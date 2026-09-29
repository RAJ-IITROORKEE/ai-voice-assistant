# handoff.md — Live Progress Tracker

> Updated at the end of every work session. Read this first when resuming.

**Last updated:** 2026-09-29
**Current phase:** Phase 3 ✅ done → Phase 4 next (Speech pipelines: realtime S2S)
**Next action:** Add `IVoicePipeline` abstraction to the relay; implement Azure Realtime + Gemini Live alongside Classic; per-turn latency instrumentation; settings-driven pipeline/model/language on device. See `docs/phases/phase-3.md` for the Phase 3 report.

---

## Overall status

| Phase | Name | Status | Gate result | Tag |
|---|---|---|---|---|
| 0 | Baseline commit & hygiene | ✅ done | committed `d280545`, tagged | `v1.0-azure-baseline` |
| 1 | Web app MVP (Vercel) | ✅ gate passed | https://luna-voice-agent.vercel.app — auth+DB+RLS live, chat streams, deployed | `v1.1` |
| 2 | LangGraph agent service (ACA) | ✅ gate passed | https://luna-agent.gentlecoast-5d201a63.centralindia.azurecontainerapps.io — OpenAI-compat SSE, JWT auth, Postgres memory; web app proxies to it | `v1.2` |
| 3 | Relay↔agent sync + conversations | ✅ gate passed | Device WS → owner resolved + device upserted; LLM via luna-agent (`source=device`) → Postgres; web app shows devices/conversations/settings live; conversation ops (pin/archive/delete) work | `v1.3` (pending commit) |
| 4 | Speech pipelines (realtime S2S) | ⬜ not started | — | `v1.4` |
| 5 | Tools & MCP | ⬜ not started | — | `v1.5` |
| 6 | Hardening & multi-device | ⬜ not started | — | `v2.0` |

Legend: ⬜ not started · 🟡 in progress · ✅ gate passed

---

## What's live right now (baseline)

- **Relay:** `luna-relay` on Azure Container Apps — `https://luna-relay.gentlecoast-5d201a63.centralindia.azurecontainerapps.io` (`/health` → `{"status":"ready"}`), image `lunarelayacr.azurecr.io/luna-relay:v8` (Phase 3: InsForge Postgres sync + AgentClient). Agent: `luna-agent:v2`.
- **Device:** ESP32-S3 flashed 2026-09-29 (IDF 5.5.4, COM16), connects to relay over WSS. Wi-Fi config = `Raj`/`Raj@0311` (verified working with `PG-WIFI` test earlier — full chain: Wi-Fi → TLS → WSS connected → ready tone).
- **Pipeline:** Azure Speech STT (en-IN) → DeepSeek-V4-Flash → Azure Speech TTS (en-IN-NeerjaNeural); Firestore memory via Azure WIF (key-less).
- **Azure models available** on `datumm-agent-resource` (eastus2): `gpt-realtime-2.1` ✅ (realtime S2S, capacity 10), gpt-5.6-luna/sol/terra, gpt-6-astra, DeepSeek-V4-Pro/Flash, grok-4.3/4.6, Kimi-K2.7-Code, text-embedding-3-small.
- **Vercel CLI:** 50.34.1, logged in as `raj-iitroorkee`.

## Key decisions (locked in, PLAN.md §6)
1. InsForge (auth+db+realtime) — in use since Phase 1.
2. Speech upgrade order: Azure realtime → Gemini Live (Phase 4).
3. Agent language: Python (Phase 2 done).
4. Agent state: InsForge Postgres (not ACA sidecars — deviation adopted in Phase 2).
5. InsForge cloud free tier.

## Blockers / needs from user
- Browser E2E verification of Phase 2 (sign in at https://luna-voice-agent.vercel.app with Google, send a chat, confirm reply + history persists).
- Google AI API key (Phase 4, Gemini Live). ElevenLabs/Deepgram/Tavily keys (optional, Phases 4–5).
- Notion/Gmail OAuth connect flows (Phase 5, via web UI).

## Session log
- **2026-09-29 (1):** Azure migration completed (GCP Cloud Run → Container Apps); WIF Firestore auth working; firmware flashed & verified online; PG-WIFI E2E connectivity test passed; Wi-Fi reverted to `Raj`. Research completed: speech models (report in `docs/research/`), web stack (assistant-ui/shadcn/InsForge/LangGraph/MCP). PLAN.md + complete_phase_wise_plan.md + handoff.md written.
- **2026-09-29 (2):** Plan approved (all default decisions: InsForge, Azure-realtime→Gemini order, Python agent, ACA sidecars, InsForge cloud). **Phase 0 done:** committed `d280545`, tagged `v1.0-azure-baseline`, remote = github.com/RAJ-IITROORKEE/ai-voice-assistant. **Phase 1 in progress:** `voice-agent-app/` scaffolded (assistant-ui: Next.js 16.3.6, React 19, AI SDK v7, Tailwind v4, base-nova style). Built AppShell (sidebar nav: Chats/Devices/Tools/MCP/Settings), dark-first ThemeProvider + ModeToggle, 4 panel components (devices/tools/mcp/settings placeholders). Moved aui components into `components/assistant-ui/elements/`. Chat route pointed at Azure Foundry OpenAI-compatible endpoint (`gpt-5.6-luna`), key in git-ignored `.env.local`. `pnpm build` ✓ all 6 routes; dev server home 200 ✓; `/api/chat` streams from Azure ✓.
- **2026-09-29 (3):** **InsForge backend LIVE.** Authed CLI via device-flow OAuth (rajrabidas001@gmail.com, Google-verified). Created project **luna-voice-agent** (id `dbe5bdcc-163a-473f-92ab-924bd784044e`, appkey `ts4hxi45`, region us-east, base `https://ts4hxi45.us-east.insforge.app`), linked to `voice-agent-app/`. OAuth providers google+github enabled by default. **DB schema applied** via `db/migrations/0001_init.sql` + `0002_auth_uid_policies.sql` (ran with `pg` node client — no local psql): 6 tables `devices, conversations, messages, settings, mcp_servers, tool_calls`, all with owner-only RLS using `auth.uid()`, indexes, `set_updated_at()` triggers, grants to anon+authenticated. **RLS verified E2E via REST**: owner JWT reads/inserts, anon gets `[]`, admin `ik_` bypasses. Critical gotcha learned: after creating tables must run `NOTIFY pgrst,'reload schema'` or PostgREST returns `{}`. Admin user rajrabidas001@gmail.com created in auth.users (id `d535784d-669c-4a6e-931b-49c9580d2a44`); `require_email_verification` set false via `insforge.toml`+`config apply` (no SMTP on free tier). InsForge anon key + base in git-ignored `.env.local` (root + app). Root `.env.local` added to `.gitignore`. SDK `@insforge/sdk` installed; `lib/insforge.ts` client created. **Next:** wire auth UI (sign-in page + guard), conversation persistence to InsForge, Vercel deploy.
- **2026-09-29 (4):** **Phase 2 done (LangGraph agent service).** Built `agent/` (Python 3.12, FastAPI + LangGraph + psycopg_pool + PyJWT). Exposes OpenAI-compatible `POST /v1/chat/completions` (SSE) — accepts InsForge JWT (JWKS-validated) OR relay `user_sub` body param. Persists to Postgres with per-conn `request.jwt.claims` so RLS applies. **Fixed Windows ProactorEventLoop vs psycopg-async** by setting `WindowsSelectorEventLoopPolicy` at module import AND running uvicorn with `loop="none"` (uvicorn's `asyncio` loop factory hardcodes ProactorEventLoop on win32, ignoring policy). Web app `/api/chat` now proxies to agent when `LUNA_AGENT_URL` set, with custom fetch that forwards the caller's Bearer token; falls back to direct-Azure otherwise. `assistant.tsx` sends `Authorization: Bearer <insforge access token>` via `AssistantChatTransport.headers` (uses `getHttpClient().getValidAccessToken()`). **Deployed:** image `lunarelayacr.azurecr.io/luna-agent:v1` via `az acr build`; ACA app `luna-agent` (min 1 / max 2 replicas, 0.5 CPU / 1 Gi, MI-based ACR pull); secrets via ACA secret store; FQDN `luna-agent.gentlecoast-5d201a63.centralindia.azurecontainerapps.io`. Vercel prod env `LUNA_AGENT_URL` + `LUNA_AGENT_MODEL` set; redeployed. **Verified:** health 200, streamed chat `luna-agent-ok` over public HTTPS, DB rows persisted, unauth → 401, JWKS reachable. Browser E2E (Google OAuth sign-in → chat) pending user verification. Phase report: `docs/phases/phase-2.md`.
