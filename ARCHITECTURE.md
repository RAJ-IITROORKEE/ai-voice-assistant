# Architecture

## Topology

`ESP32-S3 firmware -> authenticated WSS /voice -> LunaRelay (.NET 8 on Cloud Run or LAN) -> Azure Speech, chat completion endpoint, Firestore`

The firmware is an ESP-IDF application for the Atom VoiceS3R/C126-ECHO. GPIO41 is active-low push-to-talk. ES8311 I2S captures 16 kHz mono PCM16 and plays 24 kHz mono PCM16. Capture is forwarded block-by-block; it is not retained as a WAV buffer.

## Voice Turn

1. The firmware opens a persistent authenticated WSS connection and sends `turn.start`.
2. While the button is held, 20 ms microphone frames are queued and sent. On release it drains the uplink queue and sends `turn.commit`.
3. `VoiceSession` pushes PCM into Azure Speech continuous recognition and emits partial/final transcript controls.
4. Deterministic conversation and voice commands run locally in the relay. Otherwise the chat responder loads the active conversation, performs one OpenAI-compatible chat completion request, and persists the exchange.
5. Azure Speech TTS produces raw PCM. The relay buffers 1.2 seconds, sends an initial 60-frame burst, then paces 20 ms frames.
6. Firmware prebuffers playback locally, plays through I2S, and accepts cancellation at any stage.

The chat interface is streaming-shaped but currently receives one completed model response. Model tokens are not streamed and model/TTS work does not overlap.

## Wire Protocol

Protocol version 1 binary frames have a 16-byte `LUNA` header: magic, version, direction, header length, turn ID, sequence number, then PCM16 payload. Controls are UTF-8 JSON.

Client controls: `turn.start`, `turn.commit`, `turn.cancel`.

Server controls: `ready`, `turn.ready`, `transcript.delta`, `transcript.final`, `tts.start`, `response.delta`, `tts.end`, `turn.complete`, `turn.cancelled`, `turn.error`.

The ESP32 and relay protocol implementations must change together, with both .NET and host C++ protocol tests updated.

## Backend Boundaries

`AssistantContext` makes ownership, profile, and storage scope explicit. The current static device-token authenticator creates a credential-derived subject. `StaticAssistantContextResolver` maps that subject to the `default` profile and the same Firestore document key used before this refactor. A normalized `X-Device-Id` is diagnostics only and never changes ownership.

`AssistantProfile` holds recognition language, default voice, memory limits, and the enabled tool policy. Today only the default profile exists in configuration. `IConversationStore` receives a `ConversationScope` and `MemoryPolicy`, so future persisted profiles can vary retention without changing the existing conversation schema.

Firestore stores an active bounded history plus up to five compact archives. Active state expires after seven days of inactivity and Cloud Firestore TTL uses `expires_at`. Persistence errors are logged and do not prevent a response from being spoken.

`ToolRegistry` and `ToolExecutor` are empty and deny by default. They are a boundary, not an implementation of tools, MCP, OAuth, or model tool calling. An external write requires a server approval validator; an LLM or client boolean is never approval.

## Deployment And Scale

Local mode uses generated TLS on port 7443. Cloud Run terminates public TLS and forwards HTTP/WebSocket traffic to port 8080. `Deploy-LunaRelayCloudRun.ps1` manages image build, secrets, Firestore TTL, and runtime service configuration. Default Cloud Run capacity is min 1, max 3, concurrency 1. Tune capacity only after load testing Azure provider quotas and persistent WebSocket behavior.

Cloud Run is intentionally public at the transport layer because embedded devices cannot use Cloud Run IAM directly. `/voice` relies on its device token, so add token rotation, enrollment, rate limiting, and per-device credentials before a multi-device deployment.

## Future Control Plane

A web or Android application must use a separate control API and verified user identity, not `/voice`. The target flow is OIDC subject -> owner -> authorized profile -> registered devices. Device credentials must be individually issued, revocable, and bound server-side; a device header must never establish ownership.

Google Calendar/Gmail require delegated user OAuth and encrypted server-side refresh-token storage. The Cloud Run service account is not a substitute for user delegation. Future MCP support must allow only server-curated identifiers, enforce egress/time/output limits, treat tool output as untrusted input, and require short-lived server-issued approval bound to owner, tool, normalized arguments, and expiry for writes.
