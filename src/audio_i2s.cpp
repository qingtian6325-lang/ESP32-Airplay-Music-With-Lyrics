#include "audio_i2s.h"
#include <string.h>

// Pure volume math functions (available on both native and target)
int16_t audio_apply_volume(int16_t sample, uint8_t vol_percent) {
    if (vol_percent == 0) {
        return 0;
    }
    if (vol_percent >= 100) {
        return sample;
    }
    int32_t scaled = ((int32_t)sample * (int32_t)vol_percent) / 100;
    if (scaled > 32767) {
        scaled = 32767;
    } else if (scaled < -32768) {
        scaled = -32768;
    }
    return (int16_t)scaled;
}

void audio_apply_volume_buffer(int16_t *samples, size_t count, uint8_t vol_percent) {
    if (!samples || count == 0) {
        return;
    }
    if (vol_percent >= 100) {
        return;
    }
    if (vol_percent == 0) {
        memset(samples, 0, count * sizeof(int16_t));
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        samples[i] = audio_apply_volume(samples[i], vol_percent);
    }
}

// Software volume attenuation state
static uint8_t s_current_volume = 100;

void audio_i2s_set_volume(uint8_t vol_percent) {
    if (vol_percent > 100) {
        vol_percent = 100;
    }
    s_current_volume = vol_percent;
}

uint8_t audio_i2s_get_volume(void) {
    return s_current_volume;
}

#if (defined(ESP_PLATFORM) || defined(ARDUINO)) && !defined(ARDUINO_ARCH_NATIVE)

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

static SemaphoreHandle_t s_i2s_mutex = NULL;
static bool s_i2s_installed = false;

bool audio_i2s_init(void) {
    if (s_i2s_mutex == NULL) {
        s_i2s_mutex = xSemaphoreCreateMutex();
        if (s_i2s_mutex == NULL) {
            return false;
        }
    }

    if (s_i2s_installed) {
        i2s_driver_uninstall(I2S_NUM_0);
        s_i2s_installed = false;
    }

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = I2S_BCLK_PIN,
        .ws_io_num = I2S_LRC_PIN,
        .data_out_num = I2S_DIN_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        log_e("i2s_driver_install failed: %d", err);
        return false;
    }

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) {
        log_e("i2s_set_pin failed: %d", err);
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }

    i2s_zero_dma_buffer(I2S_NUM_0);
    s_i2s_installed = true;
    return true;
}

size_t audio_i2s_write(const uint8_t *data, size_t len, TickType_t timeout_ticks) {
    if (!data || len == 0 || !s_i2s_installed) {
        return 0;
    }

    if (s_i2s_mutex != NULL) {
        if (xSemaphoreTake(s_i2s_mutex, timeout_ticks) != pdTRUE) {
            return 0;
        }
    }

    size_t total_written = 0;
    uint8_t current_vol = s_current_volume;

    if (current_vol >= 100) {
        // Direct zero-copy write
        size_t written = 0;
        esp_err_t err = i2s_write(I2S_NUM_0, data, len, &written, timeout_ticks);
        if (err == ESP_OK) {
            total_written = written;
        }
    } else if (current_vol == 0) {
        // Write silence using static zero buffer
        static const size_t SILENCE_CHUNK = 256;
        static const uint8_t zero_buf[SILENCE_CHUNK] = {0};
        size_t remaining = len;
        while (remaining > 0) {
            size_t chunk = (remaining > SILENCE_CHUNK) ? SILENCE_CHUNK : remaining;
            size_t written = 0;
            esp_err_t err = i2s_write(I2S_NUM_0, zero_buf, chunk, &written, timeout_ticks);
            if (err != ESP_OK || written == 0) {
                break;
            }
            total_written += written;
            remaining -= written;
        }
    } else {
        // Attenuate chunk by chunk
        static const size_t CHUNK_BYTES = 512;
        int16_t chunk_buf[CHUNK_BYTES / sizeof(int16_t)];
        size_t remaining = len;
        const uint8_t *ptr = data;

        while (remaining > 0) {
            size_t chunk = (remaining > CHUNK_BYTES) ? CHUNK_BYTES : remaining;
            size_t num_samples = chunk / sizeof(int16_t);
            memcpy(chunk_buf, ptr, chunk);
            audio_apply_volume_buffer(chunk_buf, num_samples, current_vol);

            size_t written = 0;
            esp_err_t err = i2s_write(I2S_NUM_0, chunk_buf, chunk, &written, timeout_ticks);
            if (err != ESP_OK || written == 0) {
                break;
            }
            total_written += written;
            ptr += written;
            remaining -= written;
        }
    }

    if (s_i2s_mutex != NULL) {
        xSemaphoreGive(s_i2s_mutex);
    }

    return total_written;
}

#else
// =============================================================================
// Native / Host Mock Implementation for Unit Tests
// =============================================================================

static bool s_mock_i2s_initialized = false;

bool audio_i2s_init(void) {
    s_mock_i2s_initialized = true;
    return true;
}

size_t audio_i2s_write(const uint8_t *data, size_t len, TickType_t timeout_ticks) {
    (void)timeout_ticks;
    if (!data || len == 0 || !s_mock_i2s_initialized) {
        return 0;
    }
    // On native mock, simulate successful DMA consumption
    return len;
}

#endif
