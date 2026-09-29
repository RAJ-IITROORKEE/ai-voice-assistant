# handoff.md — Live Progress Tracker

> Updated at the end of every work session. Read this first when resuming.

**Last updated:** 2026-09-29
**Current phase:** PLANNING — awaiting plan approval
**Next action:** User reviews `PLAN.md` + `complete_phase_wise_plan.md`, answers decision points (§6 of PLAN.md), then Phase 0 begins.

---

## Overall status

| Phase | Name | Status | Gate result | Tag |
|---|---|---|---|---|
| 0 | Baseline commit & hygiene | ⬜ not started | — | `v1.0-azure-baseline` |
| 1 | Web app MVP (Vercel) | ⬜ not started | — | `v1.1` |
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
- **2026-09-29:** Azure migration completed (GCP Cloud Run → Container Apps); WIF Firestore auth working; firmware flashed & verified online; PG-WIFI E2E connectivity test passed; Wi-Fi reverted to `Raj`. Research completed: speech models (report in `docs/research/`), web stack (assistant-ui/shadcn/InsForge/LangGraph/MCP). PLAN.md + complete_phase_wise_plan.md + handoff.md written. **Awaiting plan approval.**
