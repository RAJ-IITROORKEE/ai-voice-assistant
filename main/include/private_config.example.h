#pragma once

// Copy the macro definitions into the existing root secrets.h. Do not commit
// credentials. AZURE_AI_ENDPOINT and AZURE_TTS_ENDPOINT are full HTTPS URLs.
#define WIFI_SSID "replace-with-wifi-name"
#define WIFI_PASSWORD "replace-with-wifi-password"
#define AZURE_AI_API_KEY "replace-with-azure-openai-key"
#define AZURE_AI_ENDPOINT "https://your-resource.services.ai.azure.com/openai/v1/chat/completions"
#define AZURE_AI_MODEL "your-model-deployment"
#define AZURE_SPEECH_KEY "replace-with-azure-speech-key"
#define AZURE_SPEECH_ENDPOINT "https://your-speech-resource.cognitiveservices.azure.com"
#define AZURE_TTS_ENDPOINT "https://your-region.tts.speech.microsoft.com/cognitiveservices/v1"

// Existing values can be retained or omitted to use the safe defaults below.
#define SPEECH_LANGUAGE "en-IN"
#define TTS_VOICE_ENGLISH "en-IN-NeerjaNeural"
