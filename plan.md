# Luna Voice Assistant Plan

## Status

| Phase | Status | Notes |
|---|---|---|
| Audit existing sketches and latency | Complete | Root causes and reference sketch defects are documented in `VOICE_ASSISTANT.md`. |
| Verify board/toolchain/library sources | Complete | Atom VoiceS3R pins confirmed; ESP-IDF v5.5.4 is installed at `E:\Espressif\frameworks\esp-idf-v5.5.4`; the board currently enumerates on COM14. |
| Create native ESP-IDF project | Complete | Native I2C/I2S ES8311 driver and direct-cloud pipeline boot successfully on the device. |
| Implement capture, tones and cancellation | Complete for direct MVP | 60-second PSRAM capture plus audible ready/start/stop tones are implemented. The blocking cloud greeting was removed from boot. HTTP cancellation still waits for an in-flight request to return. |
| Implement STT, SSE and continuous TTS | Complete for direct MVP | Fast Transcription is primary with short-audio fallback. Responses SSE emits bounded text segments. Dual 12-second PSRAM caches and a 14-second ordered playback ring prevent network-paced speech breaks. |
| Build, flash and inspect boot logs | Complete | Firmware flashed and hash-verified through the board's USB Serial/JTAG port. Boot reaches `READY` with PSRAM, codec, Wi-Fi and TLS validation confirmed. |
| User end-to-end voice test | Complete for continuity | User confirmed the latest flashed firmware produced a smooth, complete spoken reply. Post-fix timing telemetry still needs a dedicated capture for p50/p95 work. |

## Implementation Order

1. Preserve `secrets.h` macro compatibility and add an ignored configuration template.
2. Bring up the ES8311, I2C, I2S RX/TX, button, Wi-Fi, and TLS certificate bundle using ESP-IDF.
3. Add bounded PSRAM WAV capture, duration/no-speech checks, WAV finalization, and a cancellable state machine.
4. Validate the flashed end-to-end workflow and record continuity, latency, cancellation, and cloud-service evidence.
5. Measure Fast Transcription and Responses latency over repeated turns.
6. Move to a persistent authenticated relay for interim STT, TTS text streaming, and hard request cancellation if Google-Assistant-like latency remains required.

## Acceptance Criteria

- No insecure TLS calls.
- No response-length-dependent TTS truncation.
- The first TTS segment can synthesize while later LLM text continues streaming.
- Playback is not paced by Azure HTTP body delivery and has no audible segment breaks in the physical acceptance turn.
- Button cancels playing/generating work and enters capture quickly.
- A full 60 second capture fits in the documented 8 MB PSRAM allocation budget.
- Build completes with ESP-IDF v5.5.4 for `esp32s3`.
- A flashed device reaches `READY` and initializes codec, Wi-Fi, and cloud configuration cleanly.

## Verification Record

- The firmware console uses USB Serial/JTAG rather than the unconnected UART0 pins. The latest `idf.py build` produced a `0xf8d40` byte app with `0xf72c0` bytes (50%) free in the `0x1f0000` factory app partition.
- After a download-mode reset, the board re-enumerated from COM15 to COM14. COM14 is the active ESP32-S3 USB Serial/JTAG monitor port; COM4 and COM6 are Bluetooth ports.
- Initial COM15 probes did not reach ROM download mode. After the documented reset sequence, Windows re-enumerated the board on COM14 and subsequent direct esptool flashes succeeded.
- After the reset/download sequence, Windows re-enumerated the board as `COM14`. Its ROM identified an ESP32-S3-PICO-1 revision 0.2 with embedded 8 MB flash and 8 MB PSRAM. The bootloader, partition table, and application were written at `0x0`, `0x8000`, and `0x10000`; every segment hash verified.
- The USB Serial/JTAG boot log confirms the current image boots, detects and tests 8 MB octal PSRAM, initializes the ES8311 codec, connects to Wi-Fi, reaches `READY`, and validates a TLS certificate.
- Device telemetry proved Azure raw-PCM REST delivery could be slower than real-time and caused repeated 0.5-1 second breaks. The final direct-cloud design caches each bounded segment in one of two 576 KB PSRAM buffers, synthesizes one segment ahead, and hands clips to a 672 KB/14-second ordered playback ring with a 1.2-second initial watermark.
- Fast Transcription now uploads the completed WAV as known-length multipart without a second audio-sized copy. Its response is bounded to 64 KB PSRAM and checked for complete receipt before parsing; failure falls back once to the known-working short-audio endpoint.
- The latest image was built, flashed at 460800 baud, and hash-verified on COM14. Boot logs confirm 8 MB PSRAM, codec, Wi-Fi, `READY`, and the local ready tone with zero underruns. The user then reported one full physical reply as smooth and complete. The attempted passive serial capture did not start, so post-fix latency and underrun counters were not recorded.

## Explicit Follow-ups

- Capture at least 30 post-fix turns and report p50/p95 release-to-STT, first sentence, first PCM, and audible-start latency.
- Build the authenticated WSS relay before promising sub-3.5-second p95 or production deployment; direct REST has already shown highly variable multi-second cloud latency.
- Validate Hindi and Latin-script Hinglish across STT and available Azure Neural voices.
- Rotate existing Azure keys and replace direct cloud credentials with a relay before external deployment.
- Consider Azure Voice Live/Realtime only if true full-duplex, echo cancellation, or acoustic-tone understanding becomes required.
