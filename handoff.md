# handoff.md — Live Progress Tracker

> Updated at the end of every work session. Read this first when resuming.

**Last updated:** 2026-09-29
**Current phase:** Phase 1 ✅ done → Phase 2 next (LangGraph agent service)
**Next action:** Scaffold `agent/` (Python LangGraph), Docker → ACA `luna-agent`, route web chat through it. See `docs/phases/phase-1.md` for Phase 1 report.

---

## Overall status

| Phase | Name | Status | Gate result | Tag |
|---|---|---|---|---|
| 0 | Baseline commit & hygiene | ✅ done | committed `d280545`, tagged | `v1.0-azure-baseline` |
| 1 | Web app MVP (Vercel) | ✅ gate passed | https://luna-voice-agent.vercel.app — auth+DB+RLS live, chat streams, deployed | `v1.1` |
| 2 | LangGraph agent service (ACA) | ⬜ not started | — | `v1.2` |
| 3 | Relay↔agent sync + conversations | ⬜ not started | — | `v1.3` |
| 4 | Speech pipelines (realtime S2S) | ⬜ not started | — | `v1.4` |
| 5 | Tools & MCP | ⬜ not started | — | `v1.5` |
| 6 | Hardening & multi-device | ⬜ not started | — | `v2.0` |

Legend: ⬜ not started · 🟡 in progress · ✅ gate passed

---

## What's live right now (baseline)

- **Relay:** `luna-relay` on Azure Container Apps — `https://luna-relay.gentlecoast-5d201a63.centralindia.azurecontainerapps.io` (`/health` → `{"status":"ready"}`), image `lunarelayacr.azurecr.io/luna-relay:v6`, revision `--0000005`.
- **Device:** ESP32-S3 flashed 2026-09-29 (IDF 5.5.4, COM16), connects to relay over WSS. Wi-Fi config = `Raj`/`Raj@0311` (verified working with `PG-WIFI` test earlier — full chain: Wi-Fi → TLS → WSS connected → ready tone).
- **Pipeline:** Azure Speech STT (en-IN) → DeepSeek-V4-Flash → Azure Speech TTS (en-IN-NeerjaNeural); Firestore memory via Azure WIF (key-less).
- **Azure models available** on `datumm-agent-resource` (eastus2): `gpt-realtime-2.1` ✅ (realtime S2S, capacity 10), gpt-5.6-luna/sol/terra, gpt-6-astra, DeepSeek-V4-Pro/Flash, grok-4.3/4.6, Kimi-K2.7-Code, text-embedding-3-small.
- **Vercel CLI:** 50.34.1, logged in as `raj-iitroorkee`.

## Uncommitted work (to be committed in Phase 0)
- `AGENTS.md`, `docs/research/low-latency-speech-models-2026.md`, `relay/Deploy-LunaRelayAzure.ps1`, WIF code changes (`AgentMemory.cs`, `RelayOptions.cs`, `LunaRelay.csproj`), `.gitignore` update, plus the three plan docs.

## Key decisions pending (PLAN.md §6)
1. InsForge (default) vs Clerk+Supabase for auth/db.
2. Speech upgrade order (default: Azure realtime → Gemini Live).
3. Agent language (default: Python).
4. Agent state location (default: ACA sidecars).
5. InsForge cloud free tier (default) vs self-host.

## Blockers / needs from user
- InsForge MCP connection (user said they'll provide) — needed in Phase 1.
- Google AI API key (Phase 4, Gemini Live). ElevenLabs/Deepgram/Tavily keys (optional, Phases 4–5).
- Notion/Gmail OAuth connect flows (Phase 5, via web UI).

## Session log
- **2026-09-29 (1):** Azure migration completed (GCP Cloud Run → Container Apps); WIF Firestore auth working; firmware flashed & verified online; PG-WIFI E2E connectivity test passed; Wi-Fi reverted to `Raj`. Research completed: speech models (report in `docs/research/`), web stack (assistant-ui/shadcn/InsForge/LangGraph/MCP). PLAN.md + complete_phase_wise_plan.md + handoff.md written.
- **2026-09-29 (2):** Plan approved (all default decisions: InsForge, Azure-realtime→Gemini order, Python agent, ACA sidecars, InsForge cloud). **Phase 0 done:** committed `d280545`, tagged `v1.0-azure-baseline`, remote = github.com/RAJ-IITROORKEE/ai-voice-assistant. **Phase 1 in progress:** `voice-agent-app/` scaffolded (assistant-ui: Next.js 16.3.6, React 19, AI SDK v7, Tailwind v4, base-nova style). Built AppShell (sidebar nav: Chats/Devices/Tools/MCP/Settings), dark-first ThemeProvider + ModeToggle, 4 panel components (devices/tools/mcp/settings placeholders). Moved aui components into `components/assistant-ui/elements/`. Chat route pointed at Azure Foundry OpenAI-compatible endpoint (`gpt-5.6-luna`), key in git-ignored `.env.local`. `pnpm build` ✓ all 6 routes; dev server home 200 ✓; `/api/chat` streams from Azure ✓.
- **2026-09-29 (3):** **InsForge backend LIVE.** Authed CLI via device-flow OAuth (rajrabidas001@gmail.com, Google-verified). Created project **luna-voice-agent** (id `dbe5bdcc-163a-473f-92ab-924bd784044e`, appkey `ts4hxi45`, region us-east, base `https://ts4hxi45.us-east.insforge.app`), linked to `voice-agent-app/`. OAuth providers google+github enabled by default. **DB schema applied** via `db/migrations/0001_init.sql` + `0002_auth_uid_policies.sql` (ran with `pg` node client — no local psql): 6 tables `devices, conversations, messages, settings, mcp_servers, tool_calls`, all with owner-only RLS using `auth.uid()`, indexes, `set_updated_at()` triggers, grants to anon+authenticated. **RLS verified E2E via REST**: owner JWT reads/inserts, anon gets `[]`, admin `ik_` bypasses. Critical gotcha learned: after creating tables must run `NOTIFY pgrst,'reload schema'` or PostgREST returns `{}`. Admin user rajrabidas001@gmail.com created in auth.users (id `d535784d-669c-4a6e-931b-49c9580d2a44`); `require_email_verification` set false via `insforge.toml`+`config apply` (no SMTP on free tier). InsForge anon key + base in git-ignored `.env.local` (root + app). Root `.env.local` added to `.gitignore`. SDK `@insforge/sdk` installed; `lib/insforge.ts` client created. **Next:** wire auth UI (sign-in page + guard), conversation persistence to InsForge, Vercel deploy.
