# Luna Voice Assistant — Agent Guide

Production push-to-talk voice assistant: an ESP32-S3 device streams microphone PCM to a .NET relay over a secure WebSocket; the relay runs streaming STT → LLM → TTS and returns audio. Conversation memory persists in Firestore.

## Repository layout

| Path | Purpose |
|------|---------|
| `main/` | ESP-IDF firmware (C/C++). ES8311 codec over I2S, 16 kHz PCM uplink / 24 kHz downlink, GPIO41 push-to-talk. |
| `main/relay_client.cpp` | Wi-Fi + secure WebSocket client; reads `relay_config.h`. |
| `main/relay_config.h` | **Generated, git-ignored.** `LUNA_RELAY_URI`, `LUNA_RELAY_DEVICE_TOKEN`, `LUNA_RELAY_USE_PUBLIC_CA`. |
| `main/wifi_config.h` | **Git-ignored** Wi-Fi credentials (`WIFI_SSID`/`WIFI_PASSWORD`). |
| `relay/` | .NET 8 relay (`LunaRelay` WebSocket service + `LunaRelay.Protocol`). |
| `relay/Deploy-LunaRelayAzure.ps1` | **Current** production deploy to Azure Container Apps. |
| `relay/Deploy-LunaRelayCloudRun.ps1` | Legacy GCP Cloud Run deploy. |
| `relay/appsettings.Local.json` | **Git-ignored** secrets (device token, Azure Speech key, Azure OpenAI key). |

## Current hosting: Azure Container Apps

- **App:** `luna-relay` in RG `luna-relay-rg`, image `lunarelayacr.azurecr.io/luna-relay:vN`.
- **Environment:** `qr-relay-env` (Central India). Azure allows only **one Container Apps environment per region per subscription**, so Luna shares the existing regional environment.
- **Ingress:** external, target port `8080`, WebSocket at `/voice`, health at `/health`.
- **Auth:** static `X-Device-Token` header (matches firmware) + optional `X-Device-Id`.

### Conversation memory via Workload Identity Federation (key-less)

GCP org policy blocks service-account **key** creation, so the relay cannot use a JSON key from Azure. Instead it exchanges an **Azure Managed Identity** token for a GCP access token at startup:

1. `AgentMemory.cs` → `AzureWifCredentials` gets an Azure AD token for the user-assigned MI (`Firestore__AzureClientId`) with audience `fb60f99c-7a34-4190-8149-302f77469936` (Azure's WIF first-party app id; **no `/.default` suffix** for managed identity).
2. Trades it at `sts.googleapis.com/v1/token` against the WIF pool/provider (`luna-azure-pool` / `azure-provider`).
3. Impersonates `luna-relay-runtime@<gcp-project>.iam.gserviceaccount.com` via `iamcredentials.googleapis.com` and uses the token as a `CallCredential` for Firestore.

Config keys (env): `Firestore__AzureClientId`, `Firestore__GcpServiceAccount`, `Firestore__WorkloadIdentityPool`, `Firestore__WorkloadIdentityProvider`, `Firestore__GcpProjectNumber`, `Firestore__ProjectId`, `Firestore__ConversationCollection`.

> If Firestore init throws a 400 `invalid_grant`, the WIF provider `allowed-audiences` or the Azure token resource is misaligned. The deploy script sets both correctly.

## Deploy

```powershell
./relay/Deploy-LunaRelayAzure.ps1 -Tag v7
```

Idempotent; rebuilds the image with `az acr build` (no local Docker needed), updates the app, regenerates `main/relay_config.h`, and writes `relay/.azure-deployment.json`.

## Firmware: build / flash / monitor

ESP-IDF **5.5.4** lives at `E:\Espressif\frameworks\esp-idf-v5.5.4`; tools under `E:\Espressif\tools`; Python venv `E:\Espressif\python_env\idf5.5_py3.11_env`.

```powershell
$env:IDF_PATH='E:\Espressif\frameworks\esp-idf-v5.5.4'
$env:IDF_TOOLS_PATH='E:\Espressif'
$env:IDF_PYTHON_ENV_PATH='E:\Espressif\python_env\idf5.5_py3.11_env'
$env:Path = "E:\Espressif\python_env\idf5.5_py3.11_env\Scripts;E:\Espressif\tools\ninja\1.12.1;E:\Espressif\tools\cmake\3.30.2\bin;E:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;" + $env:Path
idf.py build          # build
idf.py -p COM16 flash # flash (device on native USB-JTAG, COM16)
idf.py -p COM16 monitor
```

> The device must be in range of the `WIFI_SSID` configured in `main/wifi_config.h`; otherwise it logs `Wi-Fi disconnected; reconnecting` until the AP appears.

## Conventions

- Never commit `appsettings.Local.json`, `relay_config.h`, `wifi_config.h`, `private_config.h`, or `.azure-deployment.json` / `.gcloud-deployment.json`.
- One voice WebSocket holds provider resources for a whole turn — scale with bounded replicas, not high per-instance concurrency.
- Firmware C uses ESP-IDF logging (`ESP_LOGx`); relay C# uses structured `ILogger`. Match existing style; minimal diffs.

<!-- INSFORGE:START -->
## InsForge backend

This project uses [InsForge](https://insforge.dev): an all-in-one, open-source Postgres-based backend (BaaS) that gives this app a database, authentication, file storage, edge functions, realtime, an AI model gateway, and payments through one platform.

- **Project:** **luna-voice-agent** (API base `https://ts4hxi45.us-east.insforge.app`)
- **Skills:** these InsForge skills are installed for supported coding agents. Reach for them before implementing any InsForge feature instead of guessing the API:
  - `insforge`: app code with the `@insforge/sdk` client (database CRUD, auth, storage, edge functions, realtime, AI, email, and Stripe payments).
  - `insforge-cli`: backend and infrastructure via the `insforge` CLI (projects, SQL, migrations, RLS policies, storage buckets, functions, secrets, payment setup, schedules, deploys).
  - `insforge-debug`: diagnosing failures (SDK/HTTP errors, RLS denials, auth and OAuth issues) and running security or performance audits.
  - `insforge-integrations`: wiring external auth providers (Clerk, Auth0, WorkOS, Better Auth, etc.) for JWT-based RLS, or the OKX x402 payment facilitator.
  - `find-skills`: discovering additional skills on demand.
- **Credentials:** app code reads keys from `.env.local`; the CLI reads `.insforge/project.json`. Never hardcode or commit keys.

Key patterns:

- Database inserts take an array: `insert([{ ... }])`.
- Reference users with `auth.users(id)`; use `auth.uid()` in RLS policies.
- For storage uploads, persist both the returned `url` and `key`.
<!-- INSFORGE:END -->
