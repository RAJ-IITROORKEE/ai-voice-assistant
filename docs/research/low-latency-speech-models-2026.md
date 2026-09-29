# Low-Latency Speech Models for a Real-Time Voice Assistant (Late 2025 / 2026)

> Research compiled Sep 2026 from official docs/pricing pages. Target: ESP32-S3 push-to-talk device streaming 16 kHz PCM16 over WebSocket to a .NET relay; multilingual (Indian English + Hindi); streaming PCM16 in/out.

---

## 1. Speech-to-Speech / Realtime Models (audio in → audio out)

| Model | Provider | Latency (typical) | Pricing | Languages | Streaming / Format | Access |
|---|---|---|---|---|---|---|
| **gpt-realtime-2.1** (GA) | OpenAI Realtime API | ~sub-300ms first-audio; WebSocket ~200ms / WebRTC ~100ms | Audio in **$32/1M tok**, cached $0.40, audio out **$64/1M tok**; text in $4/out $24 per 1M | Multilingual (incl. Hindi, Indian English) | Yes — WebSocket/WebRTC, PCM16 | `wss://api.openai.com/v1/realtime` (GA, no beta header) |
| **gpt-realtime-2.1-mini** (GA) | OpenAI Realtime API | Same transport, cheaper/smaller | Audio in **$10/1M tok**, cached $0.30, audio out **$20/1M tok**; text in $0.60/out $2.40 | Multilingual | Yes — PCM16 | Same |
| **GPT-Realtime-2.1 / 2 / 1.5, gpt-realtime-mini** (GA) | **Azure OpenAI** Realtime | WebSocket ~200ms / WebRTC ~100ms | Token-priced (same ballpark as OpenAI; Global + Data Zone tiers). `gpt-realtime-whisper`/`translate` billed **per audio hour** | Multilingual | Yes — PCM16, 24kHz, mono; WebRTC/WebSocket/SIP | `https://<res>.openai.azure.com/openai/v1/realtime` |
| **Gemini 3.8 Live** (GA) — *recommended* | Google (Agent Platform) | Sub-second streaming; native audio; affective dialog; seamless language switching | Audio in **$3.00/1M tok**, text in $0.75, audio out **$12.00/1M tok**, text out $4.50 (per-1M; **audio = 25 tok/sec**) | Multilingual, seamless code-mixing (incl. Hindi) | Yes — stateful WSS; **in: PCM16 16kHz LE; out: PCM16 24kHz LE** | `gemini-3.8-live` via Gen AI SDK / WebSocket |
| **gemini-live-2.5-flash-native-audio** (GA) | Google | Sub-second; native audio + emotional tone | Audio in **$3.00/1M tok**, text in $0.50, audio out **$12.00/1M tok**, text out $2.00 (25 tok/sec audio) | Multilingual, code-mixing | Yes — WSS, PCM16 16k in / 24k out | `gemini-live-2.5-flash-native-audio` |
| **ElevenAgents (Conversational AI)** | ElevenLabs | ~75–100ms TTS leg (Flash v2.5 / v4 Turbo); full turn managed | Per-minute via plan credits; Business "as low as **5¢/min**" all-in low-latency | 31–70+ (incl. Hindi) | Yes — WebSocket | Agents platform + custom WebSocket |
| **Deepgram Voice Agent API** | Deepgram | Low-latency managed (Flux STT + LLM + Aura TTS) | Standard **$0.075/min**; BYO-LLM **$0.059/min**; BYO LLM+TTS **$0.050/min**; Advanced $0.163/min (billed on WS connect time) | EN-focused STT (Flux multi incl. Hindi); TTS 8 langs (**no Hindi TTS**) | Yes — single WebSocket | `wss://agent.deepgram.com` |
| **Cartesia Managed Agents** (Sonic-3.6) | Cartesia | **Sub-90ms** TTS; #1 Speech Arena naturalness | Agents **$0.06/min** + $0.014/min telephony; TTS via credits | **44 languages incl. Hindi** | Yes — WebSocket | Cartesia Agents / Sonic WS |
| **xAI Grok Voice** (`grok-voice-think-fast-2.0`) | xAI | Low-latency S2S | **$0.08/min ($4.80/hr)** S2S; text input $0.004 | Multilingual | Yes — streaming | xAI Voice API |
| **AssemblyAI Voice Agent API** | AssemblyAI | Managed cascaded (U3.6 Pro RT + LLM + TTS over LiveKit) | **$0.075/min ($4.50/hr)** all-inclusive | Multilingual STT | Yes — WebSocket (PCI-certified) | `wss://agents.assemblyai.com/v1/ws` |

**Sources:** OpenAI Realtime guide + pricing, Azure OpenAI realtime-audio doc, Google Agent Platform Live API + pricing, ElevenLabs docs/pricing, Deepgram pricing, Cartesia Sonic/pricing, xAI docs, AssemblyAI pricing.

---

## 2. Fast TTS Models (streaming, for STT→LLM→TTS pipeline)

| Model | Provider | Latency (TTFB) | Pricing | Languages (Hindi?) | Streaming / Format |
|---|---|---|---|---|---|
| **Cartesia Sonic-3.6** | Cartesia | **Sub-90ms** (#1 naturalness) | Credit-based; ~133 min/mo on $5 Pro plan (≈$0.04/min eff.) | **44 langs — Hindi ✓** | Yes — WebSocket, PCM |
| **ElevenLabs Flash v2.5** (`eleven_flash_v2_5`) | ElevenLabs | **~75ms** (excl. network) | ~0.5–1 credit/char (≈ half price of Multilingual v2); Business ~5¢/min low-latency | **32 langs — Hindi ✓** | Yes — WebSocket, PCM |
| **ElevenLabs v4 Turbo** (`eleven_v4_turbo`) | ElevenLabs | **~100ms** median | Higher tier than Flash | **90+ langs — Hindi ✓** | Yes — WebSocket |
| **Deepgram Aura-2** | Deepgram | Low (WS streaming) | **$0.030/1k chars** (Aura-2); Aura-1 $0.015/1k | 8 langs — **✗ no Hindi** | Yes — REST + WSS |
| **Deepgram Flux TTS** | Deepgram | Low | **$0.0450/1k chars** | EN-focused | Yes — WSS |
| **Azure AI Speech Neural / Neural HD / Neural Flash** | Azure Speech | Low (SDK streaming) | **Neural / Neural HD Flash**: per 1M chars (Flash cheaper tier); **Neural HD** higher tier | **100+ langs — en-IN + hi-IN ✓** | Yes — Speech SDK / REST, PCM |
| **OpenAI gpt-4o-mini-tts** | OpenAI | Low | Text in $0.60/1M tok, **audio out $12.00/1M tok** | Multilingual | Yes — streaming API |
| **OpenAI tts-1 / tts-1-hd** | OpenAI | Moderate | **tts-1 $15 / 1M chars**; **tts-1-hd $30 / 1M chars** | Multilingual | Yes |
| **xAI Grok TTS** | xAI | Low | **$15 / 1M chars** | Multilingual | Yes |
| **Google Cloud TTS Chirp 3 HD** | Google Cloud | Low | per 1M chars (HD tier) | Many, incl. hi-IN | Yes |

**Sources:** ElevenLabs models doc (Flash v2.5 ~75ms, v4 Turbo ~100ms), Deepgram pricing + TTS docs, Azure TTS overview + pricing page, OpenAI pricing, xAI docs, Cartesia Sonic page.

---

## 3. Fast STT Models (streaming)

| Model | Provider | Latency | Pricing | Languages (Hindi / en-IN?) | Streaming / Format |
|---|---|---|---|---|---|
| **Deepgram Nova-3 Multilingual** | Deepgram | Low (WS streaming) | **$0.0058/min** streaming (promo; reg. $0.0092); PAYG | **hi, en-IN ✓** + 50+ langs | Yes — WSS, PCM/μ-law |
| **Deepgram Flux Multilingual** | Deepgram | **Ultra-low**, built-in turn detection | **$0.0078/min** streaming | **en, es, fr, de, hi, ru, pt, ja, it, nl ✓** | Yes — WSS (agent-optimized) |
| **ElevenLabs Scribe v2 Realtime** | ElevenLabs | **~150ms** partials | 330 credits/min (≈ STT rate) | **90+ langs — Hindi ✓** | Yes — PCM 8–48kHz, μ-law |
| **AssemblyAI Universal-Streaming Multilingual** | AssemblyAI | Low (WS) | **$0.15/hr** (session-duration billing!) | Multilingual (EN/ES/DE/FR strong; Hindi limited) | Yes — WSS |
| **AssemblyAI Universal-3.6 Pro Realtime** | AssemblyAI | Low | **$0.45/hr** (session-duration) | Multilingual | Yes — WSS |
| **Azure Speech to Text (realtime)** | Azure Speech | Low (SDK streaming) | Per audio hour (standard realtime tier) | **en-IN, hi-IN ✓** (100+ langs) | Yes — Speech SDK, PCM |
| **OpenAI gpt-4o-transcribe** | OpenAI | Low | **$0.006/min** (≈$2.50 in / $10 out per 1M tok) | Multilingual incl. Hindi | Yes |
| **OpenAI gpt-4o-mini-transcribe** | OpenAI | Low | **$0.003/min** | Multilingual | Yes |
| **gpt-live-transcribe / gpt-realtime-whisper** | OpenAI / Azure | Low (realtime session) | **$0.017/min** | Multilingual | Yes — realtime WS |
| **Whisper (OpenAI hosted)** | OpenAI | Moderate | **$0.006/min** | Multilingual incl. Hindi | Batch/file |
| **Groq whisper-large-v3-turbo** | Groq | **~216× real-time** (file) | **$0.04/hr** | Multilingual incl. Hindi | **Batch/file only (no live streaming)** |
| **Groq whisper-large-v3** | Groq | ~189× real-time | $0.111/hr | Multilingual | Batch/file only |
| **xAI Grok STT** | xAI | Low | **$0.20/hr streaming** ($0.10/hr REST) | Multilingual | Yes |

> ⚠️ **Groq Whisper caveat:** extremely fast and cheap but **file/batch-oriented** (OpenAI-compatible `/audio/transcriptions`), not a true incremental streaming STT — not ideal for a live push-to-talk partial-transcript loop unless you buffer-and-send per turn.

**Sources:** Deepgram pricing + models doc, ElevenLabs models doc, AssemblyAI pricing, Azure STT overview, OpenAI pricing, Groq speech-to-text docs, xAI docs.

---

## 4. Recommendation — ESP32 Push-to-Talk (16kHz PCM over WS → relay)

Your device already streams **16 kHz PCM16** uplink and expects low-latency audio back. Priorities: (a) end-to-end latency, (b) cost, (c) **Indian English + Hindi**, (d) PCM16 streaming.

### Best 2–3 stacks

| Rank | Stack | End-to-end latency (est.) | Cost (est./min of conversation) | Multilingual (en-IN/hi) | PCM16 fit | Why |
|---|---|---|---|---|---|---|
| **🥇 1** | **Google Gemini 3.8 Live** (native audio S2S) | **~300–600ms** (single hop, no cascade) | Audio in $3 + out $12 per 1M tok → ≈ **$0.006–0.012/min** of audio (25 tok/s) | ✓ seamless code-mix | ✓ **in 16kHz PCM16 / out 24kHz PCM16** | Lowest latency (no STT/TTS cascade), cheapest S2S, exact PCM16 match, best Hindi/code-mix. **Top pick.** |
| **🥈 2** | **Cascade: Deepgram Nova-3 Multi (STT) → Gemini/GPT LLM → ElevenLabs Flash v2.5 (TTS)** | ~500–900ms (3 hops) | STT $0.0058/min + LLM ~$0.002 + TTS ~$0.03–0.05/min → **~$0.04–0.06/min** | ✓ Nova-3 hi/en-IN + Flash hi | ✓ both stream PCM | More control (custom LLM, Firestore memory, tool use), still low latency, cheap, full Hindi both directions. |
| **🥉 3** | **OpenAI gpt-realtime-2.1-mini** (S2S) — direct or via **Azure OpenAI** | ~300–700ms | Audio in $10 / out $20 per 1M tok → **~$0.02–0.05/min** typical turn | ✓ | ✓ PCM16 (24kHz native) | Fully managed S2S on your existing Azure footprint; mini keeps cost down. Use full `gpt-realtime-2.1` for best quality. |

### Category winners (quick reference)

| Category | Winner | Runner-up | Budget pick |
|---|---|---|---|
| **S2S Realtime** | Gemini 3.8 Live (latency + cost + PCM16 match) | gpt-realtime-2.1-mini (Azure/OpenAI) | Cartesia Agents $0.06/min |
| **TTS** | ElevenLabs Flash v2.5 (~75ms, Hindi) | Cartesia Sonic-3.6 (<90ms, 44 langs, Hindi) | Azure Neural (en-IN/hi-IN) / OpenAI tts-1 $15/1M |
| **STT (streaming)** | Deepgram Nova-3 Multilingual ($0.0058/min, hi+en-IN) | ElevenLabs Scribe v2 Realtime (~150ms, 90+ langs) | OpenAI gpt-4o-mini-transcribe ($0.003/min) |

### Practical notes for Luna

- **Your relay already runs on Azure Container Apps.** If you want the simplest single-vendor path with existing billing/WIF, **Azure OpenAI `gpt-realtime-2.1-mini`** over WebSocket is the drop-in; Gemini 3.8 Live is lower-latency and cheaper but adds a second cloud.
- **Gemini 3.8 Live is the best technical fit**: native 16kHz PCM in / 24kHz PCM out matches your firmware exactly (no resample on the uplink), sub-second native-audio latency, and seamless Hindi/English code-switching — at the lowest per-minute cost of any S2S option.
- **Avoid Groq Whisper for live turns** (no true streaming partials) despite the attractive $0.04/hr.
- **Deepgram Aura-2 / Flux TTS do not support Hindi** — if you pick a Deepgram-centric agent, pair it with ElevenLabs Flash v2.5 or Azure Neural for Hindi TTS.
- For **push-to-talk** (not barge-in), you don't pay a latency penalty for disabling server-VAD; set `turn_detection: none` (OpenAI/Azure) or use manual commit (Gemini/ElevenLabs) and send end-of-turn on button release.

---

### Source URLs

- OpenAI Realtime: https://platform.openai.com/docs/guides/realtime · Pricing: https://developers.openai.com/api/docs/pricing
- Azure OpenAI Realtime: https://learn.microsoft.com/en-us/azure/ai-foundry/openai/how-to/realtime-audio · Azure OpenAI pricing: https://azure.microsoft.com/en-us/pricing/details/cognitive-services/openai-service/
- Gemini Live API: https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/live-api · Pricing: https://cloud.google.com/gemini-enterprise-agent-platform/generative-ai/pricing
- ElevenLabs models: https://elevenlabs.io/docs/models · Pricing: https://elevenlabs.io/pricing · Agents: https://elevenlabs.io/docs/conversational-ai/overview
- Deepgram pricing: https://deepgram.com/pricing · Models: https://developers.deepgram.com/docs/models-languages-overview · TTS: https://developers.deepgram.com/docs/tts-models
- Cartesia Sonic: https://cartesia.ai/sonic · Pricing: https://cartesia.ai/pricing
- Azure Speech TTS: https://learn.microsoft.com/en-us/azure/ai-services/speech-service/text-to-speech · STT: https://learn.microsoft.com/en-us/azure/ai-services/speech-service/speech-to-text
- Groq STT: https://console.groq.com/docs/speech-to-text
- xAI models/voice: https://docs.x.ai/docs/models
- AssemblyAI pricing: https://www.assemblyai.com/pricing
