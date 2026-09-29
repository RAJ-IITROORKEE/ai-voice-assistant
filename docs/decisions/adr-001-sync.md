# ADR-001 — Conversation sync: InsForge Postgres is the source of truth; Firestore is read-only archive

- **Status:** Accepted (Phase 3)
- **Date:** 2026-09-29

## Context

Luna has two front doors for conversations: the ESP32 voice device (via the .NET relay) and the
web app. Before Phase 3 they used different stores:

- The relay kept short-term voice memory in **GCP Firestore** (`luna_agent_sessions`), reached from
  Azure through Workload Identity Federation.
- The web app and the Python `luna-agent` used **InsForge Postgres** (`conversations` / `messages` /
  `devices` / `settings`), gated by row-level security (`auth.uid() = user_id`).

Two stores meant device turns never appeared in the web app and vice versa, and settings changed in
the web UI had no effect on the device.

## Decision

**InsForge Postgres is the single source of truth for conversations, devices, and settings.**
Firestore is retained as a **read-only archive** of pre-Phase-3 voice sessions; no new writes are
required for the product to function.

Concretely:

1. The relay resolves a connecting device's owning InsForge user via the `devices` table
   (`device_id → user_id`), auto-claiming unclaimed devices to a configured default owner
   (`Assistant:DefaultDeviceOwnerId`) for the single-user deployment. It bumps `last_seen`/`online`
   on connect and clears `online` on disconnect.
2. The relay routes each voice turn's LLM step to `luna-agent` (`AgentClient`), passing the owner's
   InsForge `user_sub`, a stable per-device `thread_id` (`device-<device_id>`), and `source=device`.
   The agent already persists the user + assistant messages to Postgres under the owner's RLS scope,
   so device turns appear in the web app within the app's polling interval (~2s).
3. The relay reads the owner's row from `settings` each turn and lets it override the TTS voice
   (and, in later phases, model/language/pipeline), so a web-app settings change affects the next
   device turn.
4. Firestore (`IConversationStore` / `FirestoreConversationStore`) is still constructed and used for
   the relay's legacy voice-preference memory, but it is no longer required for conversation sync.
   It is treated as an archive; a future phase may remove the dependency entirely.

## Why not keep Firestore as primary?

- The web app already lives on InsForge Postgres; making it the primary store removes a sync problem
  and a whole cloud dependency (GCP WIF) from the critical path.
- Postgres RLS gives per-user isolation for free, matching how the web app reads data.
- The agent already wrote web turns to Postgres; routing device turns through the same agent makes
  the two paths converge with minimal relay changes.

## Consequences

- **Positive:** one store, one security model, device turns visible in the web app, settings sync.
- **Cost:** the relay now needs the InsForge `DATABASE_URL` (superuser) to upsert devices / read
  settings. This is a privileged connection held only by the relay; it is never exposed to devices.
- **Fallback:** if Postgres is unreachable at connect time, the relay logs a warning and continues
  in Firestore-only mode (voice still works; the turn just doesn't sync to the web app).
- **Migration:** existing Firestore sessions are left as-is (read-only). No data is migrated; new
  conversations are Postgres-native. Historical voice memory older than the active window simply
  stops being referenced.

## Configuration

Relay env (set by `relay/Deploy-LunaRelayAzure.ps1` from `relay/appsettings.Local.json`):

- `InsForge__DatabaseUrl` (secret) — Postgres connection string.
- `InsForge__AgentUrl`, `InsForge__AgentModel` — agent endpoint + model.
- `InsForge__UseAgent` (bool) — route LLM turns through luna-agent (true) or direct Azure OpenAI.
- `InsForge__SyncEnabled` (bool) — device heartbeat + settings read.
- `Assistant__DefaultDeviceOwnerId` — InsForge user uuid that owns unclaimed devices.
