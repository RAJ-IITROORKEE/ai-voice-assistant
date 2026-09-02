#pragma once

#include <stddef.h>

// Local settings are ignored. The relay owns Azure credentials; the device only
// retains Wi-Fi and its revocable relay token in ignored local files.
#include "../private_config.h"
#include "../wifi_config.h"

#ifndef WIFI_SSID
#error "Define WIFI_SSID in main/private_config.h or main/wifi_config.h."
#endif
#ifndef WIFI_PASSWORD
#error "Define WIFI_PASSWORD in main/private_config.h or main/wifi_config.h."
#endif

#ifndef SPEECH_LANGUAGE
#define SPEECH_LANGUAGE "en-IN"
#endif
#ifndef TTS_VOICE_ENGLISH
#define TTS_VOICE_ENGLISH "en-IN-NeerjaNeural"
#endif

namespace voice_config {

constexpr int kButtonGpio = 41;
constexpr int kAmpEnableGpio = 18;
constexpr int kI2cSdaGpio = 45;
constexpr int kI2cSclGpio = 0;
constexpr int kI2sMclkGpio = 11;
constexpr int kI2sBclkGpio = 17;
constexpr int kI2sWsGpio = 3;

// M5Unified's known-working VoiceS3R data-line assignment.
constexpr int kI2sTxGpio = 48;
constexpr int kI2sRxGpio = 4;

constexpr int kCaptureRateHz = 16000;
constexpr int kTtsRateHz = 24000;
constexpr int kMaxRecordSeconds = 90;
constexpr int kMinRecordMilliseconds = 350;
constexpr int kCaptureBlockSamples = 320;
constexpr int kPlaybackBlockSamples = 480;
constexpr int kAudioRingSamples = 120000;  // Five seconds of 24 kHz PCM in PSRAM.
constexpr int kAudioPrebufferSamples = 5760;  // 240 ms local-WSS jitter cover before first speech.
constexpr int kAudioRebufferSamples = 4800;  // 200 ms recovery after a transient underrun.
constexpr uint32_t kTtsWriteTimeoutMs = 10000;
constexpr uint32_t kPlaybackDrainTimeoutMs = 15000;
constexpr int kRelayUplinkFrameBytes = 640;  // 20 ms at 16 kHz PCM16.
constexpr int kRelayDownlinkFrameBytes = 960;  // 20 ms at 24 kHz PCM16.
constexpr uint32_t kRelayUplinkFrameMilliseconds = 20;
constexpr uint32_t kRelayUplinkBacklogMilliseconds = 10000;
constexpr size_t kRelayUplinkQueueDepth = 512;  // Ten seconds plus scheduling margin.
constexpr uint32_t kRelayUplinkDrainTimeoutMs = 20000;
constexpr size_t kRelayDownlinkQueueDepth = 128;  // 2.56 seconds of 20 ms PCM frames.

}  // namespace voice_config
