#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if (defined(ESP_PLATFORM) || defined(ARDUINO)) && !defined(ARDUINO_ARCH_NATIVE)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#ifndef FreeRTOS_H
typedef uint32_t TickType_t;
#ifndef portMAX_DELAY
#define portMAX_DELAY 0xFFFFFFFFUL
#endif
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the I2S peripheral and DMA buffers for MAX98357A.
 * 
 * Configures I2S_NUM_0 in standard Philips format (16-bit stereo, 44.1kHz),
 * sets pin mappings to BCLK=25, LRC=26, DIN=27 without touching PSRAM pins.
 * 
 * @return true if initialization succeeded, false otherwise.
 */
bool audio_i2s_init(void);

/**
 * @brief Write PCM audio data to the I2S DMA pipeline.
 * 
 * Thread-safe write. Applies current software volume attenuation before transmission.
 * 
 * @param data Pointer to raw 16-bit PCM buffer (interleaved stereo or mono).
 * @param len Size of data in bytes.
 * @param timeout_ticks Timeout in FreeRTOS ticks (defaults to portMAX_DELAY).
 * @return Number of bytes actually written.
 */
#ifdef __cplusplus
size_t audio_i2s_write(const uint8_t *data, size_t len, TickType_t timeout_ticks = portMAX_DELAY);
#else
size_t audio_i2s_write(const uint8_t *data, size_t len, TickType_t timeout_ticks);
#endif

/**
 * @brief Set software volume attenuation percentage (0 - 100%).
 * 
 * Values above 100 are automatically clamped to 100%.
 * 
 * @param vol_percent Volume level (0% silence, 100% full scale).
 */
void audio_i2s_set_volume(uint8_t vol_percent);

/**
 * @brief Get current software volume attenuation percentage.
 * 
 * @return Current volume level (0 - 100%).
 */
uint8_t audio_i2s_get_volume(void);

/**
 * @brief Apply linear volume scaling to a single 16-bit PCM sample.
 * 
 * Pure function with clipping protection and parameter validation.
 * 
 * @param sample Signed 16-bit PCM input sample (-32768 to 32767).
 * @param vol_percent Volume percentage (0 to 100%). Clamped to 100% if higher.
 * @return Scaled signed 16-bit sample.
 */
int16_t audio_apply_volume(int16_t sample, uint8_t vol_percent);

/**
 * @brief Scale an array of 16-bit PCM samples in place according to volume percentage.
 * 
 * @param samples Pointer to 16-bit PCM sample buffer.
 * @param count Number of samples (not bytes).
 * @param vol_percent Volume percentage (0 to 100%).
 */
void audio_apply_volume_buffer(int16_t *samples, size_t count, uint8_t vol_percent);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_I2S_H
