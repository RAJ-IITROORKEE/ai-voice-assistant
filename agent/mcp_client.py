"""MCP (Model Context Protocol) client for the Luna agent.

Loads a user's enabled MCP servers from Postgres and returns their tools as
LangChain tools so the graph can call them alongside the built-in tools.
Guardrails:
  - read_only servers contribute only non-mutating tools (heuristic on name).
  - allowed_tools (when non-empty) whitelists which of a server's tools are exposed.
Any server that fails to connect is skipped so one bad server can't break a turn.
"""
from __future__ import annotations

from typing import Any

import db

# Heuristic: tools whose names suggest mutation are dropped for read_only servers.
_MUTATING = (
    "create", "update", "delete", "add", "remove", "write", "edit", "post",
    "send", "insert", "append", "modify", "set", "move", "archive", "publish",
)


def _is_mutating(tool_name: str) -> bool:
    n = tool_name.lower()
    return any(k in n for k in _MUTATING)


def _connections(servers: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    """Build the MultiServerMCPClient connections dict from DB rows."""
    conns: dict[str, dict[str, Any]] = {}
    for s in servers:
        transport = (s.get("transport") or "streamable_http").lower()
        # Normalize common aliases.
        if transport in ("http", "streamable-http", "streamablehttp"):
            transport = "streamable_http"
        elif transport in ("sse", "server-sent-events"):
            transport = "sse"
        entry: dict[str, Any] = {"url": s["url"], "transport": transport}
        if s.get("auth_token"):
            entry["headers"] = {"Authorization": f"Bearer {s['auth_token']}"}
        conns[s["name"]] = entry
    return conns


async def load_mcp_tools(user_sub: str) -> list[Any]:
    """Return LangChain tools from all of the user's enabled MCP servers.

    Applies read_only / allowed_tools guardrails. Returns [] if no servers or
    if none are reachable.
    """
    try:
        servers = await db.list_mcp_servers(user_sub)
    except Exception:
        return []
    if not servers:
        return []

    read_only_by_name = {s["name"]: bool(s.get("read_only")) for s in servers}
    allowed_by_name = {s["name"]: set(s.get("allowed_tools") or []) for s in servers}

    try:
        from langchain_mcp_adapters.client import MultiServerMCPClient
    except Exception:
        return []

    client = MultiServerMCPClient(_connections(servers))
    try:
        tools = await client.get_tools()
    except Exception:
        # One or more servers unreachable; don't break the turn.
        return []

    filtered: list[Any] = []
    for t in tools:
        # The adapter prefixes tool names with the server name in some versions;
        # match against either the raw name or a "<server>_<tool>" form.
        name = getattr(t, "name", "") or ""
        server = None
        for sname in read_only_by_name:
            if name == sname or name.startswith(sname + "_") or name.startswith(sname + ":"):
                server = sname
                break
        # If we can't attribute the tool to a server, keep it only if a single server is configured.
        if server is None and len(servers) == 1:
            server = servers[0]["name"]
        if server is None:
            continue
        allowed = allowed_by_name.get(server) or set()
        if allowed and name not in allowed:
            continue
        if read_only_by_name.get(server) and _is_mutating(name):
            continue
        filtered.append(t)
    return filtered
