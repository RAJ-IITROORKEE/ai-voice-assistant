# Luna Voice Assistant

Luna is a push-to-talk voice assistant for the M5Stack Atom VoiceS3R / C126-ECHO.
The ESP32-S3 streams microphone PCM over an authenticated WebSocket to a .NET 8
relay. The relay performs speech recognition, chat completion, bounded Firestore
conversation memory, and speech synthesis; cloud credentials do not live on the device.

## Current Capabilities

- Hold the button to record and release to submit. Press during processing or speech to cancel.
- 16 kHz PCM16 microphone uplink and 24 kHz PCM16 synthesized speech downlink.
- Automatic WSS reconnect, bounded PSRAM queues, playback prebuffering, and a 90-second capture limit.
- Azure Speech STT/TTS, Azure OpenAI-compatible chat completions, and Firestore-backed active memory.
- Say `start a new conversation` to reset the active chat. Explicit previous-chat questions can recall the latest archived chat.
- Say `change voice to Ava`, `change voice to Andrew`, `change voice to Brian`, `change voice to default`, or `list your voices`.

There is no wake-word detector, browser/mobile application, OAuth integration, Google email/calendar integration, MCP integration, or token-by-token model streaming yet.

## Repository Layout

- `main/`: ESP-IDF firmware for the ESP32-S3.
- `relay/`: .NET 8 WebSocket relay, setup utility, Cloud Run deployment script, and tests.
- `tests/`: host-only C++ protocol and playback-policy tests.
- `ARCHITECTURE.md`: transport, backend, identity, data, and deployment design.
- `ASSISTANT.md`: product agenda, safety boundaries, and extension rules.

The project migrated from a direct-cloud firmware experiment to the relay architecture. Retired direct-cloud code and snapshots are intentionally not part of the active build.

## Local Development

Prerequisites: ESP-IDF 5.x configured in your shell, .NET SDK 8, and local Google Application Default Credentials when Firestore persistence is used.

Ignored local configuration and credentials are required but must never be committed: `secrets.h`, `main/private_config.h`, `main/wifi_config.h`, `main/relay_config.h`, `main/certs/relay_ca.pem`, `relay/appsettings.Local.json`, and `relay/certs/relay.pfx`.

Generate local TLS, a device token, ignored relay settings, and firmware relay configuration:

```powershell
dotnet run --project relay/tools/LunaRelay.Setup -- .
```

Run the local relay:

```powershell
.\relay\Run-LunaRelay.ps1
```

Build and flash firmware after loading the ESP-IDF environment:

```powershell
idf.py build
idf.py -p <USB-Serial-JTAG-port> -b 460800 flash monitor
```

## Test

```powershell
dotnet test relay/tests/LunaRelay.Protocol.Tests/LunaRelay.Protocol.Tests.csproj
cmake -S tests -B tests/build
cmake --build tests/build --config Debug
ctest --test-dir tests/build -C Debug --output-on-failure
```

The firmware build remains `idf.py build`; the C++ host tests cover only provider-free protocol and policy code.

## Deploy To Cloud Run

Authenticate to a billed GCP project, generate local relay settings first, then run:

```powershell
gcloud auth login
gcloud config set project PROJECT_ID
.\relay\Deploy-LunaRelayCloudRun.ps1
```

The script builds from the checked-in `.gcloudignore` allow/exclude boundary, creates or updates Secret Manager values, configures Firestore TTL for `expires_at`, and writes the public WSS endpoint to ignored firmware configuration. It defaults to one warm instance, a maximum of three instances, and concurrency one because each persistent voice socket consumes a complete voice pipeline.

## Security And Limits

`/voice` currently authenticates one shared static `X-Device-Token`. `X-Device-Id` is syntax-checked diagnostic metadata, not trusted identity. Firestore memory and voice preferences are scoped to the token-derived credential identity, so clients with the same token share state. `/health` is public. Do not expose the device WebSocket as a browser API.

See `ARCHITECTURE.md` and `ASSISTANT.md` before adding identities, integrations, or a control application.
