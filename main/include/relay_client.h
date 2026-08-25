#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Initializes Wi-Fi and the persistent authenticated WSS relay transport.
esp_err_t relay_init(void);
bool relay_wait_connected(uint32_t timeout_ms);
bool relay_is_connected(void);

// Starts a 16 kHz PCM16 uplink. Each PCM block is copied into a bounded TX queue.
bool relay_start_turn(uint32_t turn_id);
bool relay_send_pcm(uint32_t turn_id, const uint8_t* pcm, size_t pcm_bytes);
bool relay_input_failed(uint32_t turn_id);

// Sends end-of-input only after every queued microphone block has left the device.
bool relay_commit_turn(uint32_t turn_id);
void relay_cancel_turn(uint32_t turn_id);

// Waits until the relay's ordered 24 kHz PCM output has drained from I2S.
bool relay_wait_turn_finished(uint32_t turn_id, uint32_t timeout_ms);
