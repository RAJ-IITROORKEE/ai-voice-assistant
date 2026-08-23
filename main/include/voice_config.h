#pragma once

#include <stddef.h>

// Local settings are ignored. Existing Azure keys remain in the historical
// root secrets.h until they are rotated and replaced with device provisioning.
#include "../private_config.h"

// secrets.h is legacy storage, so it is included only by cloud.cpp.
extern const char* AZURE_AI_API_KEY;
extern const char* AZURE_SPEECH_KEY;

#ifndef WIFI_SSID
#error "Define WIFI_SSID in the root secrets.h. See main/include/private_config.example.h."
#endif
#ifndef WIFI_PASSWORD
#error "Define WIFI_PASSWORD in the root secrets.h. See main/include/private_config.example.h."
#endif
#ifndef AZURE_AI_ENDPOINT
#error "Define AZURE_AI_ENDPOINT in the root secrets.h."
#endif
#ifndef AZURE_AI_MODEL
#error "Define AZURE_AI_MODEL in the root secrets.h."
#endif
#ifndef AZURE_SPEECH_ENDPOINT
#error "Define AZURE_SPEECH_ENDPOINT in the root secrets.h."
#endif
#ifndef AZURE_TTS_ENDPOINT
#error "Define AZURE_TTS_ENDPOINT in the root secrets.h."
#endif

#ifndef SPEECH_LANGUAGE
#define SPEECH_LANGUAGE "en-IN"
#endif
#ifndef FALLBACK_SPEECH_LANGUAGE
#define FALLBACK_SPEECH_LANGUAGE SPEECH_LANGUAGE
#endif
#ifndef TTS_VOICE_ENGLISH
#ifdef TTS_VOICE
#define TTS_VOICE_ENGLISH TTS_VOICE
#else
#define TTS_VOICE_ENGLISH "en-IN-NeerjaNeural"
#endif
#endif
#ifndef TTS_VOICE_HINDI
#define TTS_VOICE_HINDI "hi-IN-SwaraNeural"
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
constexpr int kMaxRecordSeconds = 60;
constexpr int kMinRecordMilliseconds = 350;
constexpr int kCaptureBlockSamples = 320;
constexpr int kPlaybackBlockSamples = 480;
constexpr int kAudioRingSamples = 336000;  // Fourteen seconds of 24 kHz PCM in PSRAM.
constexpr int kAudioPrebufferSamples = 28800;  // 1.2 s jitter cover before first speech.
constexpr int kAudioRebufferSamples = 4800;  // 200 ms recovery after a transient underrun.
constexpr uint32_t kTtsWriteTimeoutMs = 30000;
constexpr uint32_t kPlaybackDrainTimeoutMs = 15000;
constexpr int kSentenceBytes = 320;
constexpr size_t kTtsTextSoftLimitBytes = 160;
constexpr int kTtsPrefetchQueueDepth = 17;  // Sixteen segments plus a reserved finish marker.
constexpr size_t kTtsLookaheadBytes = 576000;  // Twelve seconds of 24 kHz PCM in PSRAM.
constexpr size_t kFastSttResponseBytes = 64 * 1024;
constexpr int kHttpTimeoutMs = 15000;
constexpr int kResponsesIdleTimeoutMs = 30000;
constexpr int kMaxAnswerTokens = 280;

constexpr const char* kTimezone = "IST-5:30";

}  // namespace voice_config
