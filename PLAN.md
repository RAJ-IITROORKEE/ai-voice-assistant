# PLAN.md — Luna Voice Assistant: Product Evolution Plan

> **Status:** PROPOSED — awaiting approval before implementation.
> **Date:** 2026-09-29 · **Baseline:** working STT→LLM→TTS pipeline (ESP32-S3 → Azure Container Apps relay → Azure Speech + Azure OpenAI, Firestore memory via WIF).

---

## 1. What we have today (baseline, preserved)

```
ESP32-S3 (GPIO41 push-to-talk)
   │  16 kHz PCM16 over secure WebSocket (binary LUNA protocol)
   ▼
Azure Container Apps: luna-relay (.NET 8)
   Azure Speech STT (en-IN) → DeepSeek-V4-Flash (LLM, Firestore memory) → Azure Speech TTS (en-IN-NeerjaNeural)
   ▼
24 kHz PCM16 audio back to device
```

- Live: `https://luna-relay.gentlecoast-5d201a63.centralindia.azurecontainerapps.io` (healthy).
- **This pipeline stays intact and usable at every phase.** All new capabilities are added alongside it, never by breaking it.

## 2. Product goals (refined from requirements)

1. **Faster, more natural voice** — evaluate and integrate low-latency speech models (including speech-to-speech "realtime" models like ChatGPT voice mode), with multilingual support (Indian English + Hindi first). Keep the classic pipeline as a selectable fallback so models can be A/B tested.
2. **Web control application** — a ChatGPT/Claude-style web app (`voice-agent-app/`, Next.js) to view/sync conversation history, control devices, manage conversations (pin/archive/delete), configure the assistant (model, voice, language, persona), chat by text (with voice input), and manage tools/MCP servers.
3. **Agentic orchestration brain** — a separate LangGraph-based agent service (Docker → Azure Container Apps) that owns prompts, instructions, decision loops, memory, guardrails, and tools (web search, MCP servers like Notion/Gmail). Both the voice device and the web app talk to it.
4. **Multi-user, multi-device foundation** — authentication (admin: rajrabidas001@gmail.com first), per-user sessions, device registry, so more devices/users can be added later.
5. **Clean, versioned repo** — phase-wise commits; each phase lands as a tested, working build; docs updated per phase.

## 3. Research findings (what's actually available — verified)

### 3.1 Speech models (full report: `docs/research/low-latency-speech-models-2026.md`)

| Category | Recommended | Latency | ~Cost | Notes |
|---|---|---|---|---|
| **Speech-to-Speech** | **Gemini 3.8 Live** (GA) | ~300–600ms | ~$0.006–0.012/min | Native audio, **16 kHz in / 24 kHz out PCM16 — exact firmware match**, Hindi/English code-mix, cheapest S2S |
| S2S (Azure-native) | **gpt-realtime-2.1** / `gpt-realtime-2.1-mini` | ~300–700ms | ~$0.02–0.05/min | **Already deployed on our Azure resource** (`datumm-agent-resource`, 10 capacity). Drop-in on existing relay |
| **TTS** | ElevenLabs Flash v2.5 / Cartesia Sonic-3.6 | 75–90ms | mid | Both support Hindi, stream PCM |
| **STT** | Deepgram Nova-3 Multilingual / ElevenLabs Scribe v2 Realtime | ~150–300ms | $0.0058/min | Both support `hi` + `en-IN` streaming |

**Corrections to common assumptions:** Groq Whisper is batch-only (no live streaming partials) — unsuitable for PTT turns. Deepgram Aura-2 TTS has **no Hindi**. Azure Neural voices do cover en-IN/hi-IN (what we use now).

**Recommended strategy (3 tiers, all selectable):**
- **Tier 0 — Classic pipeline (current, preserved):** Azure STT → LLM → Azure TTS. Free-ish, stable, our fallback.
- **Tier 1 — Azure Realtime S2S:** `gpt-realtime-2.1(-mini)` — zero new vendors, already deployed, same Azure auth. First upgrade.
- **Tier 2 — Gemini 3.8 Live S2S:** best latency/cost/PCM-match + best Hindi code-mixing. Needs a Google API key. Second upgrade.
- Optional later: ElevenLabs Flash TTS / Deepgram Nova-3 STT as swappable providers in the classic pipeline.

Model selection becomes a **relay/agent config option** (`VoicePipeline: classic | azure-realtime | gemini-live`) switchable from the web app settings, so you can test which sounds/feels best.

### 3.2 Web app stack (verified)

- **UI:** `assistant-ui` (YC-backed, used by LangChain) — ChatGPT/Claude-style Thread + ThreadList (sidebar), streaming, branching, tool UIs; scaffolds with shadcn/ui styling. + **shadcn/ui** (Tailwind v4, React 19, OKLCH colors) + **next-themes** (dark-first + light toggle) + full responsive.
- **BaaS:** **InsForge** (open-source, Apache-2.0, ~10k★) — Postgres + RLS, auth (email/OAuth: Google, GitHub, Microsoft…), realtime (Socket.IO), storage, edge functions, **official MCP server** (`https://mcp.insforge.dev/mcp`) so this very agent can manage it. Free tier: 50k MAU. SDK: `@insforge/sdk`.
  - ⚠️ InsForge is young; **Clerk** is the battle-tested alternative for auth (free 50k MRU). Decision point in Phase 1.
- **Orchestration:** **LangGraph** agent as a **standalone Docker service** (`langgraph build` → `langgraph-api` image + Postgres + Redis). Exposes `/threads`, `/runs/stream` (SSE). The web app uses `@assistant-ui/react-langgraph`; devices/relay call it over HTTPS+SSE.
  - ⚠️ Standalone server needs a `LANGGRAPH_CLOUD_LICENSE_KEY` (free for ≤100k nodes/mo dev) and must NOT run scale-to-zero (task loss). On ACA we set `minReplicas=1`.
- **MCP tools:** `langchain[mcp]` (new, beta) / `langchain-mcp-adapters` `MultiServerMCPClient` to attach remote MCP servers (Notion, Gmail, web search) with per-server guardrails (allow-lists, read-only scopes, human-in-the-loop for destructive actions).
- **Hosting:** Vercel CLI 50.34.1 installed & authenticated (`raj-iitroorkee`) — deploy web app from `voice-agent-app/`.

### 3.3 Azure resources already available (verified via CLI)

- `datumm-agent-resource` (AIServices, eastus2, RG `Datumm-new`) deployments: **gpt-realtime-2.1** (realtime!), gpt-5.6-luna/sol/terra, gpt-6-astra, DeepSeek-V4-Pro/Flash, grok-4.3/4.6, Kimi-K2.7-Code, text-embedding-3-small.
- Container Apps env `qr-relay-env` (Central India), ACR `lunarelayacr`, MI `luna-relay-identity`, WIF→GCP Firestore working.

## 4. Target architecture

```
┌──────────────┐  WSS (LUNA proto, PCM16)   ┌─────────────────────────────┐
│ ESP32-S3     │ ─────────────────────────▶ │ luna-relay (.NET 8, ACA)    │
│ device(s)    │ ◀───────────────────────── │ • Voice pipelines:          │
└──────────────┘                            │   classic STT→LLM→TTS │ S2S │
      │                                     │ • Session bridging ──────┐  │
      │                                     └──────────────────────────┼──┘
      │                                                                │ HTTPS+SSE
┌─────┴────────┐  HTTPS (auth, SSE)          ┌─────────────────────────▼──┐
│ voice-agent- │ ──────────────────────────▶ │ luna-agent (LangGraph,     │
│ app (Next.js │ ◀────────────────────────── │ Python, Docker, ACA)       │
│ on Vercel)   │                             │ • prompts, memory, loops   │
│ • chat UI    │                             │ • guardrails, tools        │
│ • settings   │                             │ • MCP client (Notion,      │
│ • devices    │                             │   Gmail, web search, …)    │
│ • MCP mgmt   │                             └───────┬────────────────────┘
└─────┬────────┘                                     │
      │ SDK                                          ▼
      ▼                                     ┌─────────────────┐   ┌──────────────┐
┌─────────────┐                             │ Postgres+Redis  │   │ LLM/Speech   │
│ InsForge    │                             │ (threads/state) │   │ providers    │
│ (auth+db+rt)│                             └─────────────────┘   │ (Azure/Google│
└─────────────┘                                                     │ /ElevenLabs…)│
                                                                    └──────────────┘
```

**Data model (InsForge/Postgres):** `users`, `devices` (device_id → owner user_id, name, firmware, last_seen), `conversations` (owner, pinned, archived, deleted), `messages` (role, text, audio refs, turn metadata), `mcp_servers` (per-user server registry, encrypted tokens), `settings` (per-user: pipeline, model, voice, language, persona). The relay writes turns; web app reads in realtime. Firestore remains for raw voice-turn transcripts initially; migrate into Postgres in Phase 3.

**Sync design:** device works independently with the cloud (unchanged). The web app connects to the same backend (relay/agent + Postgres) — conversations appear live regardless of source (device or web).

## 5. Phase roadmap (details in `complete_phase_wise_plan.md`)

| Phase | Deliverable | Gate (test to pass) |
|---|---|---|
| **0** | Repo hygiene: commit current Azure migration + research docs | clean `git status`, tagged `v1.0-azure-baseline` |
| **1** | **Web app MVP** in `voice-agent-app/`: Next.js + shadcn + assistant-ui, dark-first theme, InsForge auth (admin account), conversation list + chat (text), device panel, deploy to Vercel | login as admin, send chat, see history on Vercel URL |
| **2** | **LangGraph agent service** `agent/` (Python): minimal graph (chat + memory + guardrails), Docker image → ACA (`luna-agent`), web app chat routed through it, relay can call it | web chat answered by agent service; `/ok` healthy on ACA |
| **3** | **Relay ↔ agent integration + sync**: relay sends turns to agent, transcripts persisted to Postgres, web app shows device conversations live, conversation management (pin/archive/delete/delete-all) | talk on device → conversation appears in web app |
| **4** | **Speech model upgrades**: relay pipeline abstraction; Tier 1 `gpt-realtime-2.1-mini` S2S; Tier 2 Gemini 3.8 Live; settings UI to switch; A/B notes | device turn works on each pipeline; latency measured & documented |
| **5** | **Tools & MCP**: web-search tool; MCP server registry (Notion, Gmail), guardrails + approvals; tools panel in web UI; device voice can trigger actions | ask device "search the web for X" → spoken answer; Notion MCP read works |
| **6** | **Hardening & multi-device**: device provisioning flow, per-user devices, observability (LangSmith/App Insights), rate limits, docs, `v2.0` tag | 2nd user + device E2E; full docs review |

Each phase = branch `phase/N-<name>` → PR → merge to `main` → tag `v1.N`. **Git commits only with your explicit go-ahead per phase.**

## 6. Key decisions needed from you

1. **Auth/BaaS:** InsForge (recommended — MCP-manageable, all-in-one) vs Clerk(auth)+Supabase(db). → default: **InsForge**.
2. **Speech upgrade order:** Azure realtime first (zero new vendors) then Gemini Live (best perf). → default: **yes, that order**.
3. **Agent language:** Python LangGraph (best MCP/LangChain ecosystem) vs TypeScript. → default: **Python**.
4. **Where Postgres+Redis for agent live:** ACA containers (simple, co-located) vs InsForge Postgres + ACA Redis. → default: **ACA sidecars for agent state; InsForge for app data**.
5. InsForge: cloud free tier vs self-host on ACA. → default: **InsForge Cloud free tier** (you'll provide the MCP connection).

## 7. Risks & mitigations

| Risk | Mitigation |
|---|---|
| LangGraph standalone needs license key + non-scale-to-zero | `minReplicas=1`; free dev tier; keep agent stateless-restartable (checkpoints in Postgres) |
| InsForge maturity | abstract BaaS behind a thin data-access layer; Clerk+Supabase swap path documented |
| Realtime API cost spikes | per-user rate limits; classic pipeline stays default for device; per-pipeline usage logging |
| PCM16 ↔ provider formats | keep existing 16k↔24k resampler; Gemini Live matches natively; Azure realtime needs 24k — reuse resampler |
| Scope creep | phase gates; no phase starts until previous gate passes and you approve |

## 8. Repo structure (target)

```
AI-VoiceAssitant/
├── main/                     # ESP32-S3 firmware (unchanged role)
├── relay/                    # .NET 8 voice relay (ACA)
├── agent/                    # NEW — LangGraph orchestration service (Python, Docker)
├── voice-agent-app/          # NEW — Next.js web control app (Vercel)
├── docs/
│   ├── research/             # model & stack research (dated)
│   ├── phases/               # per-phase completion reports (phase-N.md)
│   └── decisions/            # ADRs for key architecture decisions
├── PLAN.md                   # this file
├── complete_phase_wise_plan.md
├── handoff.md                # live progress tracker
├── AGENTS.md / README.md / ARCHITECTURE.md
```

New root docs are organized; nothing in `main/` or `relay/` gets disturbed except deliberate, versioned changes.
