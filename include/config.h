#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/**
 * @file config.h
 * @brief Central hardware configuration and pin definitions for ESP32-WROVER-E
 *
 * IMPORTANT HARDWARE CONSTRAINTS:
 * ESP32-WROVER-E uses GPIO 16 and GPIO 17 internally for 8MB PSRAM (CS & D3/CLK).
 * DO NOT assign GPIO 16 or GPIO 17 to any peripheral.
 */

// =============================================================================
// I2S Digital Audio Output (MAX98357A)
// =============================================================================
#define I2S_BCLK_PIN        25      // Bit Clock (BCLK)
#define I2S_LRC_PIN         26      // Left/Right Word Clock (LRC / WS)
#define I2S_DIN_PIN         27      // Serial Data In (DIN)

// Audio stream defaults
#define AUDIO_SAMPLE_RATE       44100
#define AUDIO_BITS_PER_SAMPLE   16
#define AUDIO_CHANNELS          2

// =============================================================================
// SPI Display Interface (ST7789 / ILI9341 - 320x240 Landscape)
// =============================================================================
#define TFT_CS_PIN          5       // Chip Select (VSPI CS0)
#define TFT_DC_PIN          2       // Data / Command control
#define TFT_RST_PIN         4       // Hardware Reset
#define TFT_SCK_PIN         18      // SPI Clock (VSPI SCK)
#define TFT_MOSI_PIN        23      // SPI MOSI (VSPI MOSI)
#define TFT_BLK_PIN         21      // Backlight PWM brightness control

// Display geometry
#ifndef TFT_WIDTH
#define TFT_WIDTH           320
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT          240
#endif

// =============================================================================
// FreeRTOS Dual-Core Execution Strategy
// =============================================================================
#define CORE_AUDIO          0       // Core 0: Wi-Fi, BT A2DP, AirPlay, I2S DMA
#define CORE_UI             1       // Core 1: LVGL UI rendering, HTTP lyrics fetch

// =============================================================================
// Wi-Fi Network Credentials (Default)
// =============================================================================
#ifndef WIFI_SSID
#define WIFI_SSID           "Your_WiFi_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD       "Your_WiFi_Password"
#endif

// =============================================================================
// Lyrics Provider Configuration (lrc.cx + LRCLIB Cascading Fallback)
// =============================================================================
#define LYRICS_PROVIDER_LRCCX       1   // Official public lrc.cx (LrcAPI)
#define LYRICS_PROVIDER_LRCLIB      2   // Community LRCLIB open database

// Preferred provider (default: lrc.cx for superior Mandopop/Cantopop/global coverage)
#ifndef LYRICS_DEFAULT_PROVIDER
#define LYRICS_DEFAULT_PROVIDER     LYRICS_PROVIDER_LRCCX
#endif

// Enable fallback to secondary provider if primary returns 404 or fails
#ifndef LYRICS_ENABLE_FALLBACK
#define LYRICS_ENABLE_FALLBACK      1   // 1 = enabled, 0 = disabled
#endif

// Public API endpoints
#define LRC_CX_API_ENDPOINT        "https://api.lrc.cx/lyrics"
#define LRCLIB_API_ENDPOINT        "https://lrclib.net/api/get"

// =============================================================================
// Compile-time PSRAM Safety Asserts
// =============================================================================
#if defined(__cplusplus)
static_assert(I2S_BCLK_PIN != 16 && I2S_BCLK_PIN != 17, "I2S_BCLK_PIN conflicts with ESP32-WROVER PSRAM!");
static_assert(I2S_LRC_PIN  != 16 && I2S_LRC_PIN  != 17, "I2S_LRC_PIN conflicts with ESP32-WROVER PSRAM!");
static_assert(I2S_DIN_PIN  != 16 && I2S_DIN_PIN  != 17, "I2S_DIN_PIN conflicts with ESP32-WROVER PSRAM!");

static_assert(TFT_CS_PIN   != 16 && TFT_CS_PIN   != 17, "TFT_CS_PIN conflicts with ESP32-WROVER PSRAM!");
static_assert(TFT_DC_PIN   != 16 && TFT_DC_PIN   != 17, "TFT_DC_PIN conflicts with ESP32-WROVER PSRAM!");
static_assert(TFT_RST_PIN  != 16 && TFT_RST_PIN  != 17, "TFT_RST_PIN conflicts with ESP32-WROVER PSRAM!");
static_assert(TFT_SCK_PIN  != 16 && TFT_SCK_PIN  != 17, "TFT_SCK_PIN conflicts with ESP32-WROVER PSRAM!");
static_assert(TFT_MOSI_PIN != 16 && TFT_MOSI_PIN != 17, "TFT_MOSI_PIN conflicts with ESP32-WROVER PSRAM!");
static_assert(TFT_BLK_PIN  != 16 && TFT_BLK_PIN  != 17, "TFT_BLK_PIN conflicts with ESP32-WROVER PSRAM!");
#endif

#endif // CONFIG_H
