# Luna Voice Assistant MVP

## Product Goal

Luna is a push-to-talk voice assistant for the M5Stack Atom VoiceS3R (C126-ECHO). A user holds the hardware button to talk, releases it to submit the utterance, hears an analysing tone while cloud work is pending, and hears a clear spoken reply as soon as its first sentence is synthesized.

The target is a warm-network release-to-first-speech p50 below 2 seconds and p95 below 3.5 seconds for typical 5-10 second requests. The device is intentionally half-duplex: pressing the button while Luna is speaking cancels playback and starts a new recording.

## Target Hardware

- M5Stack Atom VoiceS3R / C126-ECHO
- ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB octal PSRAM
- ES8311 codec, MEMS microphone, NS4150B amplifier, 8 ohm 1 W speaker
- Button: GPIO41, active low
- Codec I2C: SDA GPIO45, SCL GPIO0, address 0x18
- Codec audio bus: BCLK GPIO17, WS GPIO3, MCLK GPIO11, as configured by the verified M5Unified VoiceS3R board support
- Amplifier enable: GPIO18, active high

The ESP-IDF implementation uses the same active data-line assignment as M5Unified's VoiceS3R support: ESP I2S TX GPIO48 and RX GPIO4. This is deliberately preferred over guessing from signal-label direction in a schematic.

## Current Arduino Architecture

`code.ino` is a fully serial pipeline:

```text
hold button -> collect complete WAV -> upload/final STT -> wait for full LLM JSON
-> download complete TTS PCM into the recording buffer -> play
```

It opens three new TLS connections and performs no work in parallel. This explains the observed 12-15 second delay even though all APIs return HTTP 200.

### Confirmed Current Problems

- TTS is limited by the recording allocation, so it silently truncates at about 12 seconds with PSRAM and 3-5 seconds without it.
- The LLM response is buffered fully. Long answers cannot start speaking until the model has finished.
- The reference sketch has useful SSE and sentence-pipeline ideas, but also has cross-task data races, partial-SSE false successes, an error-path playback buffer overflow, and dropped speech under queue backpressure. It is not safe to copy unchanged.
- STT is fixed to `en-IN` while the model may emit Hindi and the selected TTS voice is English-only.
- `setInsecure()` disables TLS validation. Wi-Fi/cloud values are also printed or embedded in existing code. Rotate the exposed cloud keys before external deployment.
- The root contains two `.ino` files with duplicate Arduino symbols and no sketch file matching the folder name. It is not a reproducible Arduino project layout.

## MVP Architecture

```text
button event
  -> controller state machine
     -> I2S RX / ES8311 at 16 kHz
        -> bounded PSRAM WAV capture (60 seconds maximum)
           -> Azure Speech Fast Transcription
              -> Azure Speech short-audio fallback
                 -> Azure OpenAI Responses SSE
                    -> sentence assembler
                       -> bounded TTS queue
                          -> Azure Speech TTS raw PCM stream
                             -> PSRAM jitter ring
                                -> I2S TX / ES8311 at 24 kHz
```

The first completed spoken sentence is sent to TTS while Responses SSE continues to deliver later text. TTS response bytes are inserted into a playback ring and the I2S playback task starts after a small prebuffer; full-answer or full-sentence audio is never required in RAM.

## Implementation Status

The ESP-IDF project builds for `esp32s3` with verified TLS, 60-second PSRAM WAV capture, short-audio STT, fragmented Responses SSE parsing, a bounded sentence-to-TTS worker queue, direct raw-PCM playback, an analysing tone, and an NTP-based time-of-day Luna greeting after Wi-Fi connects. It does not log keys, transcripts, or response bodies.

The direct MVP currently uses the short-audio REST endpoint rather than Fast Transcription multipart. Playback streams directly into I2S, not the target PSRAM jitter ring/prebuffer. Cancellation mutes active audio and invalidates queued sentences, but an in-flight HTTP request can take until its timeout to return; true low-latency cancellation still needs cancellable network workers. Device behavior remains unverified because the attached ESP32-S3 USB interface has not returned ROM or serial-monitor bytes.

## State Model

```text
BOOT -> CONNECTING -> GREETING -> READY -> RECORDING -> TRANSCRIBING
                                                 -> GENERATING -> SPEAKING -> READY
```

Button press while `SPEAKING`, `TRANSCRIBING`, or `GENERATING` cancels the current turn. Every queued item carries a monotonically increasing turn ID so stale cloud data cannot be spoken after cancellation.

## Cloud Contracts

- STT primary: Azure Speech Fast Transcription multipart endpoint. It is optimized for quick final transcription after release.
- STT fallback: existing Azure short-audio WAV endpoint, preserving the currently working API path.
- LLM: Azure OpenAI Responses API with `stream: true`, `store: false`, bounded output tokens, and robust SSE event parsing.
- TTS: Azure Speech REST `raw-24khz-16bit-mono-pcm`; HTTP body bytes play as they arrive.
- Locale: current configuration still defaults to `en-IN`. Hindi/Hinglish locale/voice selection is a separately tracked validation item; do not claim acoustic emotion recognition from text-only STT.

## Security Boundary

`secrets.h` remains local and excluded from version control. The firmware uses ESP-IDF's certificate bundle for Azure HTTPS connections and never enables insecure TLS. For a product deployment, move Azure credentials behind an authenticated relay; flash encryption does not make long-lived cloud keys safely extractable from a device.

## Future Low-Latency Architecture

For sub-second, continuous/duplex interaction, use one authenticated device-to-relay WebSocket. The relay should run the supported Azure Speech SDK push-audio STT and TTS text streaming APIs, hold Azure credentials, and forward binary PCM instead of base64. Do not reverse engineer Azure Speech's internal WebSocket protocol on the ESP32. This is not required for the direct-cloud MVP.

## Verification Metrics

The firmware records stage timings without logging raw audio or transcripts by default:

- release to final STT
- Responses request to first text delta
- first completed sentence to first TTS audio byte
- first TTS audio byte to I2S playback start
- release to first audible speech
- internal heap, PSRAM, task stack high-water marks, queue depth, and audio underruns
