from __future__ import annotations

from typing import Any, Annotated, Sequence

from langchain_core.messages import AIMessage, BaseMessage, HumanMessage, SystemMessage, ToolMessage
from langchain_openai import ChatOpenAI
from langgraph.graph import END, START, StateGraph
from langgraph.graph.message import add_messages
from langgraph.prebuilt import ToolNode
from typing_extensions import TypedDict

import db
from config import settings
from tools import BUILTIN_TOOLS


class AgentState(TypedDict, total=False):
    # add_messages merges new messages into the running list (handles tool-call rounds).
    messages: Annotated[list[BaseMessage], add_messages]
    user_sub: str
    thread_id: str


def _build_model() -> ChatOpenAI:
    return ChatOpenAI(
        model=settings.azure_openai_model,
        api_key=settings.azure_openai_api_key,
        base_url=settings.azure_openai_endpoint.rstrip("/"),
        streaming=True,
        temperature=0.6,
    )


def _to_lc(role: str, content: str) -> BaseMessage:
    if role == "user":
        return HumanMessage(content=content)
    if role == "assistant":
        return AIMessage(content=content)
    return SystemMessage(content=content)


async def call_model(state: AgentState) -> dict[str, Any]:
    model = _build_model().bind_tools(BUILTIN_TOOLS)
    response = await model.ainvoke(state["messages"])
    return {"messages": [response]}


def _should_continue(state: AgentState) -> str:
    last = state["messages"][-1]
    if isinstance(last, AIMessage) and last.tool_calls:
        return "tools"
    return END


async def record_tools(state: AgentState) -> dict[str, Any]:
    """Persist each tool call + result to tool_calls for web-UI visibility."""
    user_sub = state.get("user_sub")
    thread_id = state.get("thread_id")
    if not user_sub or not thread_id:
        return {}
    messages = state["messages"]
    # Pair AIMessage.tool_calls with the ToolMessages that answered them (in this round).
    tool_msgs = {
        m.tool_call_id: m for m in messages if isinstance(m, ToolMessage)
    }
    for m in messages:
        if not isinstance(m, AIMessage) or not m.tool_calls:
            continue
        for call in m.tool_calls:
            result = tool_msgs.get(call["id"])
            summary = ""
            status = "succeeded"
            if result is not None:
                summary = result.content if isinstance(result.content, str) else str(result.content)
                status = getattr(result, "status", "success") or "success"
            try:
                await db.record_tool_call(
                    user_sub, thread_id,
                    tool=call.get("name", "unknown"),
                    args=call.get("args", {}) or {},
                    result_summary=summary,
                    status=status,
                )
            except Exception:
                # Never let persistence break the turn.
                pass
    return {}


def build_graph():
    g = StateGraph(AgentState)
    tool_node = ToolNode(BUILTIN_TOOLS)
    g.add_node("call_model", call_model)
    g.add_node("tools", tool_node)
    g.add_node("record_tools", record_tools)
    g.add_edge(START, "call_model")
    g.add_conditional_edges("call_model", _should_continue, {"tools": "tools", END: END})
    g.add_edge("tools", "record_tools")
    g.add_edge("record_tools", "call_model")
    return g.compile()


graph = build_graph()
