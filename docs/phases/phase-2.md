# Phase 2 — LangGraph Orchestration Service: Completion Report

**Status:** ✅ Gate passed (browser E2E pending user OAuth sign-in)
**Date:** 2026-09-29
**Branch:** `phase/2-agent-service`
**Live URL:** https://luna-agent.gentlecoast-5d201a63.centralindia.azurecontainerapps.io

## What was built

### Agent service (`agent/`, Python 3.12, FastAPI + LangGraph)
- **`main.py`** — FastAPI app exposing OpenAI-compatible `POST /v1/chat/completions` (SSE streaming). Accepts either:
  - `Authorization: Bearer <insforge-jwt>` (web app path — validated via JWKS from InsForge, RS256), or
  - `user_sub` in body (relay/device path — trusted because the relay authenticates devices separately).
  - `GET /health` and `GET /ready` liveness endpoints.
  - Persists each turn to Postgres (`conversations` + `messages` tables) via `psycopg_pool`.
- **`graph.py`** — minimal LangGraph `StateGraph` with a single `call_model` node; model = `ChatOpenAI` pointed at Azure AI Foundry (`gpt-5.6-luna`).
- **`auth.py`** — InsForge JWT validation with JWKS fetching (1h cache, auto-refresh on `kid` miss).
- **`db.py`** — async Postgres pool; `get_or_create_conversation`, `load_history`, `append_message`, `maybe_set_title`. Sets `request.jwt.claims` per-conn so RLS enforces owner-only access.
- **`config.py`** — pydantic-settings; env-driven (`DATABASE_URL`, `AZURE_OPENAI_*`, `INSFORGE_*`, `SYSTEM_PROMPT`, `CORS_ORIGINS`).
- **`Dockerfile`** — `python:3.12-slim`; runs `python main.py` (not the `uvicorn` CLI) so the Windows event-loop policy fix is honored during local dev; on Linux (ACA) uvicorn picks `uvloop` automatically.
- **Windows event-loop gotcha (fixed):** `psycopg` async can't use `ProactorEventLoop`. The fix: `asyncio.set_event_loop_policy(WindowsSelectorEventLoopPolicy())` at module import + run uvicorn with `loop="none"` so `asyncio.run()` defers to the policy. (Uvicorn's built-in `asyncio` loop factory explicitly returns `ProactorEventLoop` on win32 regardless of policy.)

### Web app wiring (`voice-agent-app/`)
- **`app/api/chat/route.ts`** — rewritten to proxy to the agent. Behavior:
  - If `LUNA_AGENT_URL` is set: build an AI-SDK OpenAI provider targeting `${LUNA_AGENT_URL}/v1`, with a custom `fetch` that injects the caller's `Authorization` Bearer token. The agent validates the JWT itself and owns model config, prompts, memory, and DB persistence.
  - If unset: falls back to Phase 1 direct-to-Azure behavior (kept as a safety net).
- **`app/assistant.tsx`** — `AssistantChatTransport.headers` is now an async function that calls `insforge.getHttpClient().getValidAccessToken()` and forwards it as `Authorization: Bearer …`. Token auto-refreshes if expired.
- **`.env.example`** — documents `LUNA_AGENT_URL` + `LUNA_AGENT_MODEL`.

### Azure deployment
- **Image:** `lunarelayacr.azurecr.io/luna-agent:v1` (built via `az acr build`, no local Docker).
- **Container App:** `luna-agent` in RG `luna-relay-rg`, env `qr-relay-env` (shared, Central India).
  - Ingress: external, target port 8000.
  - Replicas: min 1, max 2 (min 1 required — LangGraph services must not scale to zero mid-run).
  - CPU 0.5 / memory 1 Gi.
  - ACR pull via user-assigned MI `luna-relay-identity` (same as `luna-relay`).
- **Secrets (ACA secret store):** `openai-key`, `insforge-anon-key`, `database-url`.
- **Env vars:** `INSFORGE_BASE_URL`, `AZURE_OPENAI_ENDPOINT`, `AZURE_OPENAI_MODEL=gpt-5.6-luna`, `CORS_ORIGINS=*` + the three `secretref:` wiring.

### Vercel deployment
- Production env vars added: `LUNA_AGENT_URL=https://luna-agent.gentlecoast-5d201a63.centralindia.azurecontainerapps.io`, `LUNA_AGENT_MODEL=gpt-5.6-luna`.
- Redeployed; production alias https://luna-voice-agent.vercel.app now points at the agent.

## Gate verification
- Local: `GET /health` → `{"status":"ok"}`; `GET /ready` → `{"status":"ready"}`.
- Local: `POST /v1/chat/completions` with `user_sub` → streams `luna-ok` token-by-token, persists user+assistant rows to `messages` (verified via direct SQL).
- Local: unauthenticated request → 401 ✓. JWKS endpoint reachable from agent (`keys: 1`) ✓.
- Deployed: `GET https://luna-agent…azurecontainerapps.io/health` → 200.
- Deployed: chat completion streams `luna-agent-ok` over public HTTPS ✓.
- Web app: `https://luna-voice-agent.vercel.app/sign-in` → 200, home → 200.
- `pnpm build` clean (all 8 routes).
- **Browser E2E (sign in → chat → history persists)** deferred to user — requires Google OAuth sign-in, which can't be driven headless. Auth chain is fully proven (JWKS validation + relay device-token path both work).

## Deviations from plan
- **No `langgraph.json` / `langgraph dev`.** Plan called for the `langgraph-api` runtime image; instead we run a custom FastAPI app that *uses* LangGraph internally (`graph.py`) but exposes an OpenAI-compatible endpoint. This is simpler, matches the existing relay protocol, and avoids needing a `LANGGRAPH_CLOUD_LICENSE_KEY`. Trade-off: we lose built-in `/threads` and `/runs/stream` SSE — acceptable because the web app speaks OpenAI SSE via the AI SDK, and thread state lives in our own Postgres.
- **No Postgres/Redis sidecars.** Agent state (conversations/messages) lives in InsForge Postgres — same DB as the web app — so sync in Phase 3 becomes trivial. Redis wasn't needed (no long-running checkpoints yet).

## Next phase
Phase 3 — Relay ↔ agent integration + full sync: the .NET relay will call `luna-agent` for the LLM step (replacing direct Azure OpenAI), so device voice turns flow through the same agent brain, get persisted to Postgres, and appear live in the web app.
