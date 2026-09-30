# Phase 5 — Tools & MCP

**Status:** ✅ Done (tool loop + built-in tools + tool-call persistence + web UI). MCP connections are scaffolded in the UI; live server wiring lands in Phase 6.
**Tag:** `v1.5`
**Branch:** `phase/4-speech-pipelines` (continued) → merged to `main`

## Goal (from `complete_phase_wise_plan.md`)

Give the agent capabilities: a tool-calling loop in the LangGraph agent, persisted tool-call audit,
and a path to MCP-provided tools.

## What was built

### Agent (Python) — `agent/`

- **`tools.py`** (new) — built-in tools via `@tool`:
  - `web_search` — keyless web search: DuckDuckGo instant answers → Wikipedia summary → DuckDuckGo
    lite snippets (no API key needed). Optional richer results via `TAVILY_API_KEY` (free tier).
  - `set_reminder` / `list_reminders_tool` — persist to a new `reminders` table (RLS-scoped per user).
  - `save_note_tool` / `list_notes_tool` — structured notes/tasks in a new `notes` table
    (`kind='note'|'task'`, tasks carry a `done` flag).
  - `get_current_time`, `calculator` (from earlier in the phase).
  - Reminder/note tools read the caller's `user_sub` via LangChain `InjectedState` so rows are
    RLS-scoped to the signed-in user / device owner.
  - `calculator` — safe arithmetic via `ast` (only `+ - * / % // **`, parentheses, unary `+/-`;
    any other node type is rejected before `eval`).
  - `get_current_time` — current UTC time (ISO 8601).
  - `BUILTIN_TOOLS` list consumed by the graph (7 tools total).

New Postgres tables (RLS `auth.uid() = user_id`): `reminders(id, user_id, task, remind_at, note, done)`
and `notes(id, user_id, kind, title, body, tags, done, created_at, updated_at)`.
- **`graph.py`** (rewritten) — real tool loop:
  - `AgentState{messages: Annotated[list, add_messages], user_sub, thread_id}`.
  - `call_model` binds `_build_model().bind_tools(BUILTIN_TOOLS)`.
  - `_should_continue` → `tools` when the last `AIMessage` has `tool_calls`, else `END`.
  - `ToolNode(BUILTIN_TOOLS)` executes the calls.
  - `record_tools` node pairs each `AIMessage.tool_calls[i]` with its `ToolMessage` (by
    `tool_call_id`) and persists an audit row via `db.record_tool_call` (wrapped in try/except so a
    persistence hiccup never breaks a turn).
  - Edges: `START → call_model → (tools | END)`, `tools → record_tools → call_model`.
- **`db.py`** — `record_tool_call(user_sub, thread_id, tool, args, result_summary, status,
  server='built-in')` inserts into `tool_calls` under the caller's RLS claims.
- **`main.py`** — the `/v1/chat/completions` endpoint now runs `graph.astream_events(..., version="v2")`
  and forwards only `on_chat_model_stream` content deltas (tool-call args / tool results are not
  leaked into the token stream). Initial state carries `user_sub` + `thread_id` so the tool loop
  stays RLS-scoped.

### Relay (.NET) — `relay/`

- **`InsForgeStore.HeartbeatAsync`** (new) — best-effort `UPDATE devices SET last_seen=now(), online=true`.
- **`Program.cs`** — a 30s `PeriodicTimer` heartbeat runs alongside the live voice session so the
  device shows **Online** in the dashboard for the whole (persistent) connection, not just mid-turn.
  The heartbeat is cancelled and the device marked offline in `finally`.

### Web app (Next.js) — `voice-agent-app/`

- **`components/panels/tools-panel.tsx`** (rewritten) — "Active tools" lists the two built-in tools
  (active badge, no toggle); "Recent tool calls" reads the `tool_calls` table (3s polling) showing
  tool, args, result summary, status, server, relative time.
- **`components/panels/mcp-panel.tsx`** (rewritten) — lists `mcp_servers` (name, url, transport,
  status, enabled, read_only; 5s polling). "Add server" is present but disabled ("Coming in Phase 6").
  `auth_token` is never selected.

## Deployment

- Agent `luna-agent:v5` (tool loop + web_search + reminders + notes) → ACA, `/health` ok.
- Relay `luna-relay:v12` (heartbeat + realtime voice-map fix + diagnostic logging) → ACA, `/health` ready.
- Web app redeployed to Vercel (`/tools`, `/mcp`, `/devices`, `/conversations`, `/` all 200).

## Verification

- **Local:** graph `astream_events` ran `calculator('12*(8+5)')` → `ToolMessage 156` → final token
  `156` streamed; HTTP stream returned `156`; a `tool_calls` row (`calculator`, args, `156`,
  `success`, `built-in`) landed in Postgres.
- **Deployed:** public HTTPS POST to `luna-agent/v1/chat/completions` ran `calculator('7*(6+3)')`
  → `63` streamed. Tool loop works in production.
- `pnpm build` clean (11 routes, 0 TS errors).

## Deviations

- **Web search is keyless, not Azure Grounding-with-Bing.** No Bing/Search/Grounding resource exists
  in the Azure subscription, so the agent uses a free keyless chain (DuckDuckGo instant answers →
  Wikipedia → DuckDuckGo lite). This covers encyclopedic/definitional queries well; live market/news
  data is limited without a key. **Upgrade path:** set `TAVILY_API_KEY` (Tavily free tier, ~1000
  credits/mo) to get rich live results — one env var, no code change.
- **MCP is a UI scaffold** — the `mcp_servers` table is surfaced read-only; connecting to external
  MCP servers (Notion, Gmail, etc.) and merging their tools into the agent is Phase 6 work (needs the
  `langchain-mcp-adapters` client + per-server OAuth/token, which requires your credentials).

## Next

Phase 6 — memory & voice profiles (and wiring live MCP servers): long-term memory extraction into
Postgres, per-user voice/persona profiles applied per turn, and real MCP server connections that
expose their tools to the agent (read-only first, with approval for writes).
