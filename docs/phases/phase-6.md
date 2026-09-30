# Phase 6 — Tools, MCP & UI polish

**Status:** 🟡 In progress (MCP client + built-in tools + UI live; Notion wiring + memory/profiles next)
**Tag:** `v1.6` (pending)

## What shipped so far

### Agent (Python) — `agent/`

- **`mcp_client.py`** (new) — per-user MCP connections:
  - `load_mcp_tools(user_sub)` reads enabled rows from `mcp_servers`, builds a
    `MultiServerMCPClient` (normalizes `transport` to `streamable_http`/`sse`, adds
    `Authorization: Bearer <auth_token>` when present), and returns the server's tools.
  - Guardrails: a `read_only` server drops mutating tools (name heuristic: create/update/delete/
    add/…); a non-empty `allowed_tools` array whitelists. An unreachable server is skipped so it
    never breaks a turn.
- **`graph.py`** — `call_model` binds `BUILTIN_TOOLS + mcp_tools` (MCP tools lazily loaded and
  cached per run); a custom `run_tools` node resolves each tool call by name across both sets and
  wraps results in `ToolMessage`. AgentState carries `user_sub`/`thread_id`/`mcp_tools`.
- **`tools.py`** — added `web_search` (keyless: DuckDuckGo instant answers → Wikipedia summary →
  DDG lite scrape; optional `TAVILY_API_KEY` upgrade), `set_reminder`/`list_reminders_tool`,
  `save_note_tool`/`list_notes_tool`. Reminder/note tools read the caller's `user_sub` via LangChain
  `InjectedState`. **7 built-in tools total.**
- **`db.py`** — `add_reminder`/`list_reminders`/`save_note`/`list_notes`/`list_mcp_servers`, all
  RLS-scoped (`auth.uid() = user_id`).
- New Postgres tables (RLS): `reminders(id,user_id,task,remind_at,note,done,created_at)`,
  `notes(id,user_id,kind,title,body,tags[],done,created_at,updated_at)`.
- Deps: `langchain-mcp-adapters 0.3.2`, `mcp 1.30.0`.

### Web app — `voice-agent-app/`

- **`mcp-panel.tsx`** — working **Add server** dialog (name, url, transport, auth_token, read_only,
  allowed_tools) inserting into `mcp_servers`; per-row delete. Server list polls 5s.
- **`tools-panel.tsx`** — lists the 7 built-in tools (active) + the last 20 `tool_calls` (3s poll).
- **UI/UX fix (all panels)** — root cause of clipped rows: panels used `h-full overflow-y-auto`
  inside a parent `overflow-hidden` without a resolved height. Fixed by making the content wrapper a
  flex column (`main` with `min-h-0 flex-1`) and panels `min-h-0 flex-1 overflow-y-auto`. Rows
  redesigned to compact single-line cards (truncating title + inline badges + always-visible icon
  actions + pin indicator). Applies to Conversations, Tools, Devices, Settings, MCP.

## Verification

- Deployed **agent v6** (`/health` ok). Live MCP E2E: asked about the LangGraph repo → the agent
  used the registered **DeepWiki MCP** tool (`ask_wiki_question`) and answered correctly; the call
  was logged to `tool_calls`.
- Built-in tools verified live earlier (web_search answered "President of India"; reminders/notes
  persisted to their tables).
- `pnpm build` clean (11 routes, 0 TS errors); web app deployed to Vercel, all pages 200.

## Known gaps / next

- **Notion MCP** — needs your Notion integration token (add via the MCP panel: Notion's hosted MCP
  endpoint + Bearer token, read_only off if you want page creation). Client + UI are ready.
- **tool_calls.server attribution** — MCP calls currently log `server='built-in'`; will stamp the
  real server name (minor).
- **Memory & voice/persona profiles** — durable per-user facts injected into the system prompt, and
  per-turn voice/persona from `settings`.
- **Device S2S test** on relay v12 — press GPIO41; the relay logs diagnostic frames/VAD events to
  root-cause the earlier realtime timeout.
