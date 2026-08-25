# Luna Relay

The relay keeps Azure credentials off the ESP32 and provides one authenticated,
persistent WebSocket for microphone audio, cancellation, streamed model output,
and raw speaker PCM.

## Local Setup

Generate the ignored local certificate, device token, relay settings, and firmware
relay configuration from the existing ignored firmware configuration:

```powershell
dotnet run --project relay/tools/LunaRelay.Setup -- .
.\relay\Run-LunaRelay.ps1
```

`Run-LunaRelay.ps1` runs the relay in the current terminal. It checks that the
ignored local settings exist, builds the project when needed, then keeps the
authenticated HTTPS/WSS server running until you press `Ctrl+C`.

The relay listens on HTTPS/WSS port 7443. Windows Firewall must permit inbound TCP
7443 on the private network. Do not commit `appsettings.Local.json`, the PFX file,
or `main/relay_config.h`.

## Azure Container Apps

Use the cloud relay when the access point blocks device-to-device traffic. The
deployment keeps Azure credentials as Container App secrets and uses Azure-managed
public TLS, so the firmware trusts the normal ESP-IDF certificate bundle rather than
a locally generated certificate.

1. Authenticate interactively with `az login`.
2. Run the deployment from the repository root:

```powershell
.\relay\Deploy-LunaRelayContainerApp.ps1
```

The script creates a Basic Azure Container Registry, a managed identity with only
`AcrPull`, a Container Apps environment, and one warm external relay replica in East
US. It reads ignored local settings only in memory, writes secrets directly to Azure,
then rewrites the ignored device relay configuration with the public WSS endpoint.
It writes ignored deployment state to prevent accidental duplicate charged resources.

After deployment, rebuild and flash the firmware. The active Container App endpoint
is `wss://<public-fqdn>/voice`; do not put a custom certificate or Azure key on the
device.

## Google Cloud Run

Cloud Run is the preferred public-hosting path when Wi-Fi client isolation blocks
the Atom from reaching a PC-hosted relay. It provides managed public TLS for WSS and
keeps the Azure credentials in Google Secret Manager.

1. Authenticate and select a billed GCP project:

```powershell
gcloud auth login
gcloud config set project PROJECT_ID
```

2. Deploy one warm relay in `us-east4`:

```powershell
.\relay\Deploy-LunaRelayCloudRun.ps1
```

The script enables only Cloud Run, Cloud Build, Artifact Registry, and Secret
Manager APIs. It creates a private Docker repository and a runtime service account
that may read only this relay's three secrets. The Cloud Run service allows one
concurrent persistent voice session and retains one warm instance for latency.
It then writes the ignored firmware relay endpoint using the public Google-managed
certificate. Build and flash the firmware after the script completes.

## Wire Contract

Control messages are UTF-8 JSON. Microphone and speaker audio are binary frames
with a 16-byte `LUNA` header containing protocol version, direction, turn ID, and
sequence number. Audio is signed little-endian PCM16: 16 kHz mono uplink and
24 kHz mono downlink.
