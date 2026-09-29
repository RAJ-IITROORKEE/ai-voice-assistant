# Phase 3 — Relay ↔ agent integration + full sync

**Status:** ✅ Done
**Tag:** `v1.3`
**Branch:** `phase/3-relay-agent-sync` → merged to `main`

## Goal (from `complete_phase_wise_plan.md`)

Voice device turns flow through the same agent brain; all conversations (device + web) synced to
Postgres; conversation management complete.

**Gate:** push-to-talk on device → reply plays on device AND conversation appears in web app within
~2s; pin/archive/delete sync; settings change affects next device turn.

## What was built

### Relay (.NET) — `relay/src/LunaRelay/`

- **`InsForgeStore.cs`** (new) — Npgsql access to InsForge Postgres:
  - `ResolveOwnerAndTouchAsync(device_id, firmware, defaultOwnerId)` — resolve `device_id → user_id`,
    auto-claim unclaimed devices to the default owner, bump `last_seen`/`online`.
  - `MarkOfflineAsync(device_id)` — clear `online` on disconnect.
  - `LoadSettingsAsync(user_id)` — read the owner's `settings` row.
  - `ToNpgsqlConnectionString` — converts the `postgresql://` URI (psycopg-style) into Npgsql's
    key=value form (Npgsql does not accept URIs).
- **`AgentClient.cs`** (new) — `IAgentResponder` that POSTs the current user transcript to
  `luna-agent /v1/chat/completions` over SSE with `user_sub` (owner), a stable per-device
  `thread_id` (`device-<device_id>`), and `source=device`. Streams deltas back for TTS.
- **`Program.cs`** — registers `InsForgeStore` when sync/agent enabled; chooses `IAgentResponder`
  (`AgentClient` when `InsForge:UseAgent`, else `AzureOpenAiChatClient`); resolves the device owner
  and touches the device on `/voice` connect; marks offline on disconnect; wraps InsForge resolution
  in a guard so a Postgres hiccup degrades to Firestore-only instead of breaking voice.
- **`VoiceSession.cs`** — loads the owner's settings each turn and lets the settings `voice`
  override the stored/default TTS voice (friendly name or raw Azure service name).
- **`RelayOptions.cs`** — added `InsForgeOptions` + validation; `RelayConfiguration` gains `InsForge`.
- **`AssistantProfile.cs`** — added `Assistant:DefaultDeviceOwnerId`.
- **`LunaRelay.csproj`** — added `Npgsql 8.0.5`.
- **`Dockerfile`** — restore+publish in one layer (splitting them dropped
  `Microsoft.Extensions.Logging.Abstractions` in the publish layer on ACR).

### Agent (Python) — `agent/`

- `ChatRequest` gains `source`; `db.get_or_create_conversation(..., source)` inserts conversations
  with `source='device'` for relay turns. No other change — persistence was already Postgres-native.

### Web app (Next.js) — `voice-agent-app/`

- **`components/panels/devices-panel.tsx`** — live `devices` for the signed-in user, online badge,
  relative `last_seen`, working Refresh, 3s polling, empty state.
- **`components/panels/settings-panel.tsx`** — wired to `settings` (load on mount, Save upserts by
  `user_id`): pipeline / model / voice / language / persona. ModeToggle card unchanged.
- **`components/panels/conversations-panel.tsx`** (new) — conversations browser (web + device),
  source badge, relative time, pin/unpin, archive/unarchive, soft-delete, delete-all (confirm),
  read-only message viewer; 2s polling.
- **`app/conversations/page.tsx`** (new) + **`components/app-shell.tsx`** — "Conversations" nav item.

> Realtime note: `@insforge/sdk@1.5.2` exposes channel pub/sub (`connect/subscribe/publish/on`) but
> **no Postgres-changes feed**, so the UI polls (devices 3s, conversations 2s) to meet the ~2s gate.

## Configuration / deployment

- Relay `appsettings.Local.json` gains an `InsForge` section and `Assistant:DefaultDeviceOwnerId`.
- `relay/Deploy-LunaRelayAzure.ps1` now sets the `InsForge__*` env vars and the
  `insforge-database-url` secret idempotently on both create and update.
- Postgres: added `UNIQUE (device_id)` on `devices` so the relay's `ON CONFLICT (device_id)` upsert
  works.
- Deployed: relay `luna-relay:v7` → `v8` (Npgsql URI fix + resilience), agent `luna-agent:v2`
  (source param), web app redeployed to Vercel.

## Verification

- Relay builds clean (0 warnings/errors). `/health` returns ready.
- WebSocket upgrade to `/voice` (valid device token + `X-Device-Id`) → `101`; relay auto-claimed
  `esp32-b43a45bd1d9c`, set `last_seen`, and cleared `online` on close (verified in Postgres).
- Device-style turn POSTed to `luna-agent` with `user_sub` + `thread_id=device-...` +
  `source=device` streamed `luna-device-ok` and persisted a `source='device'` conversation +
  user/assistant messages owned by the admin user.
- Web app `pnpm build` clean; `/`, `/conversations`, `/devices`, `/settings`, `/sign-in` all 200 on
  the production Vercel URL.
- Full physical push-to-talk E2E (ESP32 → relay → agent → web) verified by firmware test after
  flashing (see handoff).

## Deviations from the plan

- **Settings sync scope:** this phase wires the **voice** override into the device turn (the
  user-visible TTS setting). Model/language/pipeline settings are stored and read but take effect on
  the device in Phase 4 (pipelines) — the relay plumbing (`LoadSettingsAsync`) is already in place.
- **Conversation ops** are soft-delete based (`deleted=true`) and enforced by RLS ownership; rows
  remain in the DB.

## Next

Phase 4 — speech model upgrades (pluggable pipelines): `IVoicePipeline` abstraction, Azure Realtime
and Gemini Live alongside Classic, per-turn latency instrumentation, and settings-driven
model/language/pipeline selection on the device.
