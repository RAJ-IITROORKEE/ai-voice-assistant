# Luna Voice Assistant Plan

## Status

| Phase | Status | Notes |
|---|---|---|
| Audit existing sketches and latency | Complete | Root causes and reference sketch defects are documented in `VOICE_ASSISTANT.md`. |
| Verify board/toolchain/library sources | Complete | Atom VoiceS3R pins confirmed; ESP-IDF v5.5.4 is installed at `E:\Espressif\frameworks\esp-idf-v5.5.4`; the board currently enumerates on COM14. |
| Create native ESP-IDF project | Complete | Native I2C/I2S ES8311 driver and direct-cloud pipeline boot successfully on the device. |
| Implement capture, tone, greeting and cancellation | In progress | 60-second PSRAM capture, analysis tone, NTP time-of-day Luna greeting, and cancel/mute monitor are implemented. HTTP cancellation still waits for an in-flight request to return. |
| Implement STT, SSE and streaming TTS | In progress | Short-audio STT, Responses SSE, bounded sentence worker queue, bounded 1 MiB sentence cache, and PSRAM playback ring/prebuffer are implemented. Fast Transcription multipart remains pending. |
| Build, flash and inspect boot logs | Complete | Firmware flashed and hash-verified through the board's USB Serial/JTAG port. Boot reaches `READY` with PSRAM, codec, Wi-Fi and TLS validation confirmed. |
| User end-to-end voice test | Ready | Stable boot/runtime validation is complete. User should now test hold-to-record, release-to-submit, status tone, greeting, and response playback. |

## Implementation Order

1. Preserve `secrets.h` macro compatibility and add an ignored configuration template.
2. Bring up the ES8311, I2C, I2S RX/TX, button, Wi-Fi, and TLS certificate bundle using ESP-IDF.
3. Add bounded PSRAM WAV capture, duration/no-speech checks, WAV finalization, and a cancellable state machine.
4. Validate the flashed end-to-end workflow and record continuity, latency, cancellation, and cloud-service evidence.
5. Add Fast Transcription multipart with the existing short-audio endpoint as a safe fallback.
6. Make HTTP work cancellable so a button press can start the next recording without a request-timeout wait.

## Acceptance Criteria

- No insecure TLS calls.
- No response-length-dependent TTS truncation.
- First sentence can begin speaking before the LLM stream completes.
- Button cancels playing/generating work and enters capture quickly.
- A full 60 second capture fits in the documented 8 MB PSRAM allocation budget.
- Build completes with ESP-IDF v5.5.4 for `esp32s3`.
- A flashed device reaches `READY` and initializes codec, Wi-Fi, and cloud configuration cleanly.

## Verification Record

- The firmware console now uses USB Serial/JTAG rather than the unconnected UART0 pins. The latest `idf.py build` produced a `0xfb5c0` byte app with `0xf4a40` bytes (49%) free in the `0x1f0000` factory app partition.
- After a download-mode reset, the board re-enumerated from COM15 to COM14. COM14 is the active ESP32-S3 USB Serial/JTAG monitor port; COM4 and COM6 are Bluetooth ports.
- Flash attempts at 460800 and 115200 baud, a no-reset ROM probe, a native USB-reset probe, and a two-minute passive sync probe all failed with no serial data. `idf.py monitor` opened COM15 at 115200 for one minute and received no bytes. A second direct-port, known-data-cable download-mode attempt also failed with no-reset ROM sync. OpenOCD could not open a USB/JTAG interface (`LIBUSB_ERROR_NOT_FOUND`); Windows exposes only the CDC interface. No erase or flash write occurred.
- After the reset/download sequence, Windows re-enumerated the board as `COM14`. Its ROM identified an ESP32-S3-PICO-1 revision 0.2 with embedded 8 MB flash and 8 MB PSRAM. The bootloader, partition table, and application were written at `0x0`, `0x8000`, and `0x10000`; every segment hash verified.
- The USB Serial/JTAG boot log confirms the current image boots, detects and tests 8 MB octal PSRAM, initializes the ES8311 codec, connects to Wi-Fi, reaches `READY`, and validates a TLS certificate.
- Initial network-paced playback under-ran. The replacement image caches each bounded TTS sentence in 1 MiB PSRAM, then plays it through a 96 KiB PSRAM ring with a 400 ms prebuffer. The latest image was flashed and hash-verified on COM14. A 40-second post-boot monitor run showed no playback-underrun warnings; user listening verification remains required.

## Explicit Follow-ups

- Measure actual p50/p95 latency on the target Wi-Fi before committing to a relay.
- Validate Hindi and Latin-script Hinglish across STT and available Azure Neural voices.
- Rotate existing Azure keys and replace direct cloud credentials with a relay before external deployment.
- Consider Azure Voice Live/Realtime only if true full-duplex, echo cancellation, or acoustic-tone understanding becomes required.
