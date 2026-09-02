# Luna Relay

The .NET 8 relay keeps provider credentials off the ESP32 and provides the
authenticated WSS voice pipeline: Azure Speech STT/TTS, one OpenAI-compatible
chat completion, and bounded Firestore conversation state. The active protocol
and deployment architecture are documented in [`../ARCHITECTURE.md`](../ARCHITECTURE.md).
Product behavior and safe extension rules are in [`../ASSISTANT.md`](../ASSISTANT.md).

## Local Setup

Generate the ignored local certificate, device token, relay settings, and firmware
relay configuration from the existing ignored firmware configuration:

```powershell
dotnet run --project relay/tools/LunaRelay.Setup -- .
.\relay\Run-LunaRelay.ps1
```

Optionally add the Firestore project ID as the final setup argument. Otherwise the
Google client discovers it from the local application-default credentials.

`Run-LunaRelay.ps1` runs the relay in the current terminal. It checks that the
ignored local settings exist, builds the project when needed, then keeps the
authenticated HTTPS/WSS server running until you press `Ctrl+C`.

The relay listens on HTTPS/WSS port 7443. Windows Firewall must permit inbound TCP
7443 on the private network. Do not commit `appsettings.Local.json`, the PFX file,
or `main/relay_config.h`.

## Google Cloud Run

Cloud Run is the preferred public-hosting path when Wi-Fi client isolation blocks
the Atom from reaching a PC-hosted relay. It provides managed public TLS for WSS and
keeps the Azure credentials in Google Secret Manager.

1. Authenticate and select a billed GCP project:

```powershell
gcloud auth login
gcloud config set project PROJECT_ID
```

2. Deploy the relay in `us-east4`:

```powershell
.\relay\Deploy-LunaRelayCloudRun.ps1
```

The script enables Cloud Run, Cloud Build, Artifact Registry, Secret Manager, and
Firestore. It creates a private Docker repository and a runtime service account with
Firestore data access and access to the device-token, Speech, and Azure AI Foundry
secrets. It defaults to one warm instance, a maximum of three instances, and
concurrency one for persistent voice sessions. Use deployment parameters to tune
those limits after load testing.
It then writes the ignored firmware relay endpoint using the public Google-managed
certificate. Build and flash the firmware after the script completes.

## Wire Contract

Control messages are UTF-8 JSON. Microphone and speaker audio are binary frames
with a 16-byte `LUNA` header containing protocol version, direction, turn ID, and
sequence number. Audio is signed little-endian PCM16: 16 kHz mono uplink and
24 kHz mono downlink.
