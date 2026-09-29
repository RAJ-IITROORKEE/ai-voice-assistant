from __future__ import annotations

from typing import Any, Sequence

from langchain_core.messages import AIMessage, BaseMessage, HumanMessage, SystemMessage
from langchain_openai import ChatOpenAI
from langgraph.graph import END, START, StateGraph
from typing_extensions import TypedDict

from config import settings


class AgentState(TypedDict, total=False):
    messages: list[BaseMessage]


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
    model = _build_model()
    messages: Sequence[BaseMessage] = state["messages"]
    response = await model.ainvoke(messages)
    return {"messages": [response]}


def build_graph():
    g = StateGraph(AgentState)
    g.add_node("call_model", call_model)
    g.add_edge(START, "call_model")
    g.add_edge("call_model", END)
    return g.compile()


graph = build_graph()
