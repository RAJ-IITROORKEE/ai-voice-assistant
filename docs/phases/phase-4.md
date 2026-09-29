# Phase 4 — Speech pipelines (realtime speech-to-speech)

**Status:** ✅ Done (device E2E pending your physical button test)
**Tag:** `v1.4`
**Branch:** `phase/4-speech-pipelines` → merged to `main`

## Goal (from `complete_phase_wise_plan.md`)

Pluggable speech pipelines; upgrade speech models; the device can use a realtime speech-to-speech
model for much lower latency and more natural interaction than the Classic STT→LLM→TTS chain.

## What was built

### Pipeline abstraction — `relay/src/LunaRelay/Pipelines/`

- **`IVoicePipeline.cs`** — `IVoicePipeline` contract, `VoiceTurnRequest`, `IVoiceTurnEvents`,
  `IVoiceTurnSink`, and `PerTurnLatency` (stamps STT/agent/TTS stages; `Summary()` for logs,
  `ToMetadata()` for message metadata).
- **`VoicePipelineFactory.cs`** — `VoicePipelineIds` (`classic` / `azure-realtime` / `gemini-live`)
  and `ResolveId(settings)`: the user's settings `pipeline` overrides the configured default;
  unavailable pipelines (missing keys/config) fall back to Classic so a bad setting never breaks voice.
- **`AzureRealtimeVoicePipeline.cs`** — full realtime speech-to-speech over `gpt-realtime-2.1`
  using the **GA v1 protocol** (`/openai/v1/realtime?model=…`, `api-key` header, nested
  `audio.input/output` session shape, `response.output_audio.*` events). Streams 16 kHz device mic,
  upsampled to 24 kHz (`Resample16kTo24k`), with `server_vad` turn detection; receives 24 kHz PCM16
  assistant audio directly (no separate STT/TTS legs). Driven via `AzureRealtimeTurnContext`.
- **`GeminiLiveVoicePipeline.cs`** — scaffold; throws `NotSupportedException` until
  `GeminiLive:ApiKey` is configured (needs a Google AI API key).

### VoiceSession wiring — `relay/src/LunaRelay/VoiceSession.cs`

- `ActiveTurn` now carries `PipelineId`, a `MicFrames` channel, a `PerTurnLatency`, and the running
  `RealtimeRun` task. `Speech` (the Classic STT turn) is only created for the Classic pipeline.
- `StartTurnAsync` resolves the pipeline up-front (settings override → factory), then either starts
  the Classic `StreamingSpeechTurn` **or** opens the realtime session immediately so mic audio
  streams during the button hold.
- `HandleAudio` routes mic frames to `StreamingSpeechTurn` (Classic) or into the `MicFrames`
  channel (realtime/Gemini), stamped with `MarkFirstMic`.
- `CommitTurn` closes the mic channel (signal to the realtime pump that the button was released) and
  kicks off `ProcessTurnAsync`.
- `ProcessTurnAsync` returns early for realtime turns (already handled); Classic path is otherwise
  byte-identical, now with `MarkSttFinal/AgentFirstToken/AgentComplete/TtsFirstAudio/TtsComplete/Completed`
  and an end-of-turn latency summary log.
- `SessionRealtimeTurnContext` adapts the device session to the pipeline: emits transcript/response
  control messages, and `QueueSpeakerFrame` sends realtime 24 kHz PCM through the same
  paced/sequenced speaker path as Classic TTS.

### Configuration — `relay/src/LunaRelay/RelayOptions.cs`, `Program.cs`

- `AzureOpenAiOptions`: `RealtimeModel` (default `gpt-realtime-2.1`), `RealtimeApiVersion`.
- `GeminiLiveOptions`: `ApiKey`, `Model` (default `gemini-2.5-flash-native-audio-latest`).
- `PipelinesOptions`: `Default` (default `classic`).
- `RelayConfiguration` gains `GeminiLive` + `Pipelines`; DI registers `VoicePipelineFactory` and
  `AzureRealtimeVoicePipeline` (both stateless singletons).

### Settings → device

`DeviceSettings` gains `Pipeline`; `LoadSettingsAsync` reads the `settings.pipeline` column. Setting
the row's `pipeline` to `azure-realtime` makes the next device turn use the realtime S2S pipeline;
`classic` (or an unconfigured value) keeps the existing chain.

## Deployment

- Relay image `luna-relay:v9` built via `az acr build` (temp context includes
  `relay/Directory.Build.props`, required by the Dockerfile) and deployed to `luna-relay` on ACA;
  `/health` → ready, no startup errors.
- No firmware changes required — the device protocol (`turn.ready` / mic frames / `tts.start` /
  speaker PCM / `turn.complete`) is unchanged; only the server-side engine differs.

## Verification

- Relay builds clean (0 warnings/errors) and runs on ACA with the new DI registrations.
- Classic pipeline unchanged (byte-identical audio path) — regression-safe.
- Realtime pipeline validated at the protocol level (correct v1 URL, `api-key` auth, nested session
  shape, 16→24 kHz upsample, `response.output_audio.*` handling). **Physical E2E (button press →
  S2S reply) is pending your test** with `settings.pipeline='azure-realtime'`.

## Deviations / notes

- **Realtime is server-VAD push-to-talk**: the relay streams mic while the button is held and lets
  Azure `server_vad` detect end-of-speech; on button release the mic channel closes and the model
  finishes. `create_response=true` auto-generates the reply (no manual `response.create`).
- **Latency metadata** is logged per turn; persisting `ToMetadata()` onto the assistant message row
  is wired in the latency tracker and can be surfaced in the web app in a later polish pass.
- **Gemini Live** is scaffolded behind the factory; enable by setting `GeminiLive:ApiKey`.

## Next

Phase 5 — Tools & MCP: wire `ToolExecutor`/`ToolContracts` into the agent, web-search/calendar/etc.
tools, MCP server connections from the web app, and tool-call visibility in the UI.
