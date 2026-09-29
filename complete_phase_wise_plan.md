# Complete Phase-Wise Implementation Plan

> Companion to `PLAN.md`. Each phase: scope → tasks → files touched → test gate → commit/tag. Nothing proceeds to the next phase until the gate passes and the user approves.

---

## PHASE 0 — Baseline commit & repo hygiene

**Goal:** Lock in the current working Azure state as the versioned baseline.

**Tasks**
1. Verify `.gitignore` covers: `appsettings.Local.json`, `relay_config.h`, `wifi_config.h`, `secrets.h`, `private_config.h`, `.azure-deployment.json`, `.gcloud-deployment.json`, `node_modules/`, `.env*`, `build/`, `.vercel`.
2. Stage + commit: `AGENTS.md`, `docs/research/low-latency-speech-models-2026.md`, `relay/Deploy-LunaRelayAzure.ps1`, `relay/src/LunaRelay/*` WIF changes, `.gitignore`.
3. Commit `PLAN.md`, `complete_phase_wise_plan.md`, `handoff.md`.
4. Tag `v1.0-azure-baseline`.

**Gate:** `git status` clean; relay health `{"status":"ready"}`; device connects (verified 2026-09-29 with PG-WIFI test).

---

## PHASE 1 — Web app MVP (`voice-agent-app/`)

**Goal:** Working ChatGPT-style web app, deployed on Vercel, with auth + chat + conversation history + device panel.

**Stack:** Next.js 15 (App Router) · Tailwind v4 + shadcn/ui (new-york) · next-themes (dark-first) · assistant-ui (`@assistant-ui/react`) · `@insforge/sdk` (auth + db + realtime) · Vercel hosting.

**Tasks**
1. Scaffold: `npx assistant-ui@latest create voice-agent-app` (Next.js + AI SDK), then convert data layer to InsForge.
2. InsForge project setup **via InsForge MCP** (user provides MCP connection): tables `users`, `devices`, `conversations`, `messages`, `settings`, `mcp_servers` + RLS policies; enable Google OAuth; create admin user `rajrabidas001@gmail.com` (role=admin).
3. UI shell (dark-first, light toggle, responsive):
   - Left sidebar: conversation list (new chat, search, pin/archive), bottom: settings/devices/MCP/tools entries.
   - Main: assistant-ui `Thread` (streaming), composer with text input + mic button (Web Speech API for voice input initially).
   - Devices panel: list devices from `devices` table (id, name, last_seen, firmware, status).
   - Settings: model select, voice, language, persona (persisted per user).
4. Chat API route: temporary direct-to-Azure-OpenAI (gpt-5.6-luna) streaming via AI SDK; persist messages to InsForge. (Phase 2 swaps this to the LangGraph agent.)
5. Conversation management: pin / archive / delete / delete-all.
6. Deploy: `vercel link && vercel deploy` from `voice-agent-app/`; env vars via Vercel CLI; custom domain optional.

**Files:** `voice-agent-app/**`, `docs/phases/phase-1.md`.
**Gate:** login as admin → new chat → streamed reply → history persists across reload → dark/light toggle → visible on public Vercel URL. Update `handoff.md`.
**Commit:** `phase/1-web-app` → merge → tag `v1.1`.

---

## PHASE 2 — LangGraph orchestration service (`agent/`)

**Goal:** Standalone Python LangGraph agent in Docker on Azure Container Apps; web app chat flows through it.

**Stack:** Python 3.12 · LangGraph (`create_agent`) · `langgraph-cli` · `langchain[mcp]` (later) · Docker (`langchain/langgraph-api` base) · ACA app `luna-agent` (minReplicas=1) · Postgres+Redis sidecars (agent state/checkpoints).

**Tasks**
1. `agent/` scaffold: `langgraph.json`, `graph.py` (chat graph: system prompt from config, memory via checkpointer, guardrail node), `auth.py` (validate InsForge JWT / device token), `requirements.txt`.
2. Config-driven persona/prompts (from InsForge `settings` or env).
3. Local dev: `langgraph dev` + test with curl/SSE.
4. Dockerfile + `az acr build` → `lunarelayacr.azurecr.io/luna-agent:v1` → deploy ACA `luna-agent` (internal ingress first; `/ok` health).
5. Web app chat route → `@assistant-ui/react-langgraph` against `luna-agent` (SSE streaming).
6. LLM routing: model selectable per user (gpt-5.6-luna default; DeepSeek-V4-Flash cheap; gpt-6-astra quality).

**Files:** `agent/**`, `voice-agent-app/app/api/chat/*`, `docs/phases/phase-2.md`.
**Gate:** web chat answered by LangGraph service; thread state survives reload; `/ok` healthy; model switch works from settings.
**Commit:** `phase/2-agent-service` → merge → tag `v1.2`.

---

## PHASE 3 — Relay ↔ agent integration + full sync

**Goal:** Voice device turns flow through the same agent brain; all conversations (device + web) synced to Postgres; conversation management complete.

**Tasks**
1. Relay (.NET): add `AgentClient` (HttpClient+SSE) — turn transcript → `luna-agent` run → response text (for classic TTS) back. Keep Firestore writer as fallback flag.
2. Relay writes/reads `conversations`/`messages` in InsForge Postgres (device_id → user mapping via `devices` table).
3. Web app: realtime subscription (InsForge realtime) so device conversations appear live; device panel shows live status (last_seen heartbeat from relay).
4. Conversation ops wired to Postgres: pin/archive/delete/delete-all (RLS enforced).
5. Settings → device behavior: model/voice/language changes picked up by relay on next turn (relay polls or receives config push).
6. Migration note: keep Firestore as read-only archive; document in ADR.

**Files:** `relay/src/LunaRelay/**`, `voice-agent-app/**`, `docs/phases/phase-3.md`, `docs/decisions/adr-001-sync.md`.
**Gate:** push-to-talk on device → reply plays on device AND conversation appears in web app within ~2s; pin/archive/delete sync; settings change affects next device turn.
**Commit:** `phase/3-relay-agent-sync` → merge → tag `v1.3`.

---

## PHASE 4 — Speech model upgrades (pluggable pipelines)

**Goal:** Speech-to-speech pipelines alongside classic; A/B testable from settings.

**Tasks**
1. Relay: `IVoicePipeline` abstraction — `ClassicPipeline` (current, untouched) | `AzureRealtimePipeline` | `GeminiLivePipeline`. Config: `Voice:Pipeline` per user/device.
2. **Tier 1 — Azure Realtime:** connect relay to `gpt-realtime-2.1-mini` (deploy `gpt-realtime-2.1-mini` on `datumm-agent-resource` if mini preferred; `gpt-realtime-2.1` already exists, capacity 10). WebSocket server→server, PCM16 24kHz (reuse resampler). Map LUNA turn events ↔ Realtime API events.
3. **Tier 2 — Gemini 3.8 Live:** Google AI API key (user provides); native 16k in / 24k out PCM16 (no resample). 
4. Latency instrumentation: per-turn timings (stt_ms, llm_ms, tts_ms, e2e_ms) logged + shown in web app message details.
5. Optional: ElevenLabs Flash v2.5 / Deepgram Nova-3 as providers in Classic pipeline (behind config; keys from user).
6. Settings UI: pipeline + voice + language selectors.

**Files:** `relay/src/LunaRelay/Pipelines/**`, `voice-agent-app/app/settings/**`, `docs/phases/phase-4.md`.
**Gate:** device turn works on all 3 pipelines; latency table documented in phase-4.md; user picks default.
**Commit:** `phase/4-speech-pipelines` → merge → tag `v1.4`.

---

## PHASE 5 — Tools & MCP

**Goal:** Agent can act: web search + MCP servers (Notion, Gmail, …) with guardrails; tools visible/manageable in web UI.

**Tasks**
1. Built-in tools: `web_search` (Tavily/Bing — key from user), `device_control` (e.g., future commands), `memory_store`.
2. MCP: `langchain[mcp]` `MCPAdapter` / `MultiServerMCPClient`; per-user MCP server registry in `mcp_servers` table (URL, auth token encrypted, enabled, allowed_tools, read_only flag).
3. Guardrails: tool allow-list per server; destructive actions (send email, delete) require **human-in-the-loop approval** surfaced in web UI (and skipped/declined on device with spoken notice); rate limits; audit log table `tool_calls`.
4. Web UI: MCP section — add/remove/test server (health check), per-server tool list, enable toggles; Tools panel — all active tools with source + status.
5. Device path: device turn → agent uses tools → spoken summary ("I found 3 results…") + full detail in web app.
6. Seed servers: Notion MCP, Gmail MCP (OAuth tokens via user connect flow).

**Files:** `agent/tools/**`, `agent/mcp/**`, `voice-agent-app/app/(mcp|tools)/**`, `docs/phases/phase-5.md`.
**Gate:** "Search the web for today's weather" via device → spoken answer; Notion MCP connected from UI and readable by agent; destructive action triggers approval in web UI.
**Commit:** `phase/5-tools-mcp` → merge → tag `v1.5`.

---

## PHASE 6 — Hardening, multi-device, docs

**Tasks**
1. Device provisioning: web UI generates device token + QR/link; firmware config generator (`relay_config.h` content download).
2. Multi-user RLS audit; second test user + device E2E.
3. Observability: LangSmith tracing for agent; ACA log queries documented; per-user usage dashboards (turn counts, latency, est. cost).
4. Rate limiting + spend guards (per pipeline per user).
5. Docs sweep: README, ARCHITECTURE.md, AGENTS.md refresh; phase reports complete; `v2.0` tag.

**Gate:** full E2E with 2 users/devices; docs review pass.
**Commit:** tag `v2.0`.

---

## Standing rules for every phase
- Branch `phase/N-name` → PR → merge `main` → tag.
- Update `handoff.md` + `docs/phases/phase-N.md` before merging.
- No secrets committed; env vars via Vercel/ACA secret stores.
- Classic pipeline + device must stay working after every phase (regression check).
- Git mutations only with explicit user approval.
