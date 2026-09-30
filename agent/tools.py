from __future__ import annotations

import ast
import operator
from datetime import datetime, timezone
from typing import Annotated, Any, Optional

import httpx
from langchain_core.tools import InjectedToolArg, tool
from langgraph.prebuilt import InjectedState

from config import settings


@tool
def get_current_time() -> str:
    """Return the current UTC date and time (ISO 8601). Use when the user asks for the
    current time or date."""
    return datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


_BINOPS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.truediv,
    ast.Mod: operator.mod,
    ast.FloorDiv: operator.floordiv,
    ast.Pow: operator.pow,
}


def _eval(node: Any) -> float:
    if isinstance(node, ast.Expression):
        return _eval(node.body)
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
        return float(node.value)
    if isinstance(node, ast.BinOp):
        op = _BINOPS.get(type(node.op))
        if op is None:
            raise ValueError("operator not allowed")
        return op(_eval(node.left), _eval(node.right))
    if isinstance(node, ast.UnaryOp):
        val = _eval(node.operand)
        if isinstance(node.op, ast.UAdd):
            return val
        if isinstance(node.op, ast.USub):
            return -val
    raise ValueError("expression not allowed")


@tool
def calculator(expression: str) -> str:
    """Evaluate a basic arithmetic expression (numbers, + - * / % // ** and parentheses).
    Use for any math the user asks you to compute. Example: '2 * (3 + 4)'."""
    try:
        result = _eval(ast.parse(expression, mode="eval"))
    except Exception as e:
        return f"Could not evaluate '{expression}': {e}"
    return str(int(result)) if result == int(result) else f"{result:.6g}"


# ---- Web search -----------------------------------------------------------

async def _ddg_search(query: str, max_results: int) -> str:
    """Keyless DuckDuckGo instant-answer search (no API key)."""
    async with httpx.AsyncClient(timeout=15) as client:
        r = await client.get(
            "https://api.duckduckgo.com/",
            params={"q": query, "format": "json", "no_html": 1, "skip_disambig": 1},
        )
        data = r.json()
    out: list[str] = []
    if data.get("AbstractText"):
        out.append(data["AbstractText"])
        if data.get("AbstractURL"):
            out.append(f"Source: {data['AbstractURL']}")
    for topic in (data.get("RelatedTopics") or [])[: max_results]:
        if isinstance(topic, dict) and topic.get("Text"):
            out.append(f"- {topic['Text']}")
    if out:
        return "\n".join(out)
    # DDG instant answers only cover static topics; try Wikipedia, then DDG lite HTML.
    wiki = await _wiki_search(query)
    if not wiki.startswith("No instant answer"):
        return wiki
    return await _ddg_lite(query)


_UA = {
    "User-Agent": "LunaVoiceAssistant/1.5 (https://luna-voice-agent.vercel.app; personal project)",
    "Accept": "application/json",
}


async def _wiki_search(query: str) -> str:
    """Keyless Wikipedia summary fallback (covers encyclopedic topics)."""
    try:
        async with httpx.AsyncClient(timeout=15, headers=_UA) as client:
            sr = await client.get(
                "https://en.wikipedia.org/w/api.php",
                params={"action": "opensearch", "search": query, "limit": 1, "format": "json"},
            )
            titles = sr.json()
            if len(titles) < 2 or not titles[1]:
                return "No instant answer found. Try rephrasing the query."
            title = titles[1][0]
            sm = await client.get(
                "https://en.wikipedia.org/api/rest_v1/page/summary/" + title.replace(" ", "_")
            )
            if sm.status_code != 200:
                return "No instant answer found. Try rephrasing the query."
            data = sm.json()
    except Exception:
        return "No instant answer found. Try rephrasing the query."
    extract = data.get("extract", "")
    url = data.get("content_urls", {}).get("desktop", {}).get("page", "")
    return f"{extract}\nSource: {url}" if extract else "No instant answer found. Try rephrasing the query."


async def _ddg_lite(query: str) -> str:
    """Keyless DuckDuckGo HTML result titles+snippets as a final fallback."""
    import re
    try:
        async with httpx.AsyncClient(timeout=15, headers={**_UA, "Accept": "text/html"}) as client:
            r = await client.post("https://lite.duckduckgo.com/lite/", data={"q": query})
            html = r.text
    except Exception:
        return "No instant answer found. Try rephrasing the query."
    # Extract result snippets from the lite table rows.
    snippets = re.findall(r'<td[^>]*class="result-snippet"[^>]*>(.*?)</td>', html, re.S)
    if not snippets:
        return "No instant answer found. Try rephrasing the query."
    out = []
    for s in snippets[:5]:
        text = re.sub(r"<[^>]+>", "", s).strip()
        text = re.sub(r"\s+", " ", text)
        if text:
            out.append("- " + text)
    return "\n".join(out) if out else "No instant answer found. Try rephrasing the query."


async def _tavily_search(query: str, max_results: int) -> str:
    """Richer web search via Tavily (requires TAVILY_API_KEY)."""
    async with httpx.AsyncClient(timeout=20) as client:
        r = await client.post(
            "https://api.tavily.com/search",
            json={
                "api_key": settings.tavily_api_key,
                "query": query,
                "max_results": max_results,
                "include_answer": True,
            },
        )
        data = r.json()
    out: list[str] = []
    if data.get("answer"):
        out.append(data["answer"])
    for res in (data.get("results") or [])[: max_results]:
        title = res.get("title", "")
        content = res.get("content", "")
        url = res.get("url", "")
        out.append(f"- {title}: {content} ({url})")
    return "\n".join(out) if out else "No results found."


@tool
async def web_search(query: str, max_results: int = 5) -> str:
    """Search the web for current information and return a concise answer with sources.
    Use for facts, news, definitions, or anything the user asks you to look up online."""
    if settings.tavily_api_key:
        return await _tavily_search(query, max_results)
    return await _ddg_search(query, max_results)


# ---- Reminders + notes/tasks (persist to Postgres via db.py) ----------------

import db  # noqa: E402  (import after settings to avoid cycles)


@tool
async def set_reminder(
    task: str,
    state: Annotated[dict, InjectedState],
    remind_at: Optional[str] = None,
    note: Optional[str] = None,
) -> str:
    """Set a reminder for the user. 'task' is what to remember (e.g. 'call mom').
    'remind_at' is an optional ISO 8601 datetime (UTC) when the reminder is due.
    Use when the user says 'remind me to...' or 'set a reminder...'."""
    user_sub = (state or {}).get("user_sub")
    if not user_sub:
        return "Could not save the reminder (no user context)."
    rid = await db.add_reminder(user_sub, task, remind_at, note)
    when = f" at {remind_at}" if remind_at else ""
    return f"Reminder saved{when}: {task} (id {rid[:8]})."


@tool
async def list_reminders_tool(
    state: Annotated[dict, InjectedState],
    include_done: bool = False,
) -> str:
    """List the user's reminders. Use when the user asks 'what are my reminders' or
    'what do I need to do'."""
    user_sub = (state or {}).get("user_sub")
    if not user_sub:
        return "Could not load reminders (no user context)."
    rows = await db.list_reminders(user_sub, include_done)
    if not rows:
        return "You have no reminders."
    lines = []
    for r in rows:
        mark = "x" if r["done"] else " "
        when = f" @ {r['remind_at']}" if r["remind_at"] else ""
        lines.append(f"[{mark}] {r['task']}{when}")
    return "\n".join(lines)


@tool
async def save_note_tool(
    title: str,
    state: Annotated[dict, InjectedState],
    body: Optional[str] = None,
    kind: str = "note",
    tags: Optional[list[str]] = None,
) -> str:
    """Save a structured note or task. 'kind' is 'note' (info) or 'task' (actionable, has done).
    Use when the user says 'take a note', 'save this', 'add a task', 'note down...'."""
    user_sub = (state or {}).get("user_sub")
    if not user_sub:
        return "Could not save the note (no user context)."
    kind = "task" if kind == "task" else "note"
    nid = await db.save_note(user_sub, title, body, kind, tags)
    label = "Task" if kind == "task" else "Note"
    return f"{label} saved: {title} (id {nid[:8]})."


@tool
async def list_notes_tool(
    state: Annotated[dict, InjectedState],
    kind: Optional[str] = None,
    open_only: bool = False,
) -> str:
    """List the user's saved notes/tasks. 'kind' filters to 'note' or 'task';
    'open_only' shows only unfinished tasks. Use when the user asks about their notes or tasks."""
    user_sub = (state or {}).get("user_sub")
    if not user_sub:
        return "Could not load notes (no user context)."
    rows = await db.list_notes(user_sub, kind, open_only)
    if not rows:
        return "No notes/tasks found."
    lines = []
    for r in rows:
        mark = "x" if r["done"] else " "
        prefix = "task" if r["kind"] == "task" else "note"
        lines.append(f"[{mark}] ({prefix}) {r['title']}")
    return "\n".join(lines)


# Tools exposed to the agent.
BUILTIN_TOOLS = [
    get_current_time,
    calculator,
    web_search,
    set_reminder,
    list_reminders_tool,
    save_note_tool,
    list_notes_tool,
]
