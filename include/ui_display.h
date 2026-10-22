#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lyrics_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AudioSourceType {
    SOURCE_NONE = 0,
    SOURCE_BLUETOOTH,
    SOURCE_AIRPLAY
} AudioSourceType;

typedef struct UIState {
    char title[128];
    char artist[128];
    AudioSourceType source;
    uint32_t current_ms;
    uint32_t total_ms;
    bool wifi_connected;
    bool ap_setup_mode;
    int active_lyric_index;
    char lyric_window[5][128]; // -2, -1, 0 (active), +1, +2
} UIState;

/**
 * @brief Initialize display hardware, LVGL layout, and UI state.
 *
 * @return true if initialized successfully, false otherwise.
 */
bool ui_init(void);

/**
 * @brief Tick handler for LVGL timer and periodic UI animations.
 *
 * @param delta_ms Elapsed time in milliseconds since the last tick.
 */
void ui_task_tick(uint32_t delta_ms);

/**
 * @brief Update the currently active track title, artist, and audio source.
 *
 * @param title Track title string.
 * @param artist Artist string.
 * @param source Active audio source (SOURCE_NONE, SOURCE_BLUETOOTH, SOURCE_AIRPLAY).
 */
void ui_update_track(const char *title, const char *artist, AudioSourceType source);

/**
 * @brief Update playback elapsed and total durations.
 *
 * Automatically triggers lyric window recalculation at current_ms.
 *
 * @param current_ms Current playback position in milliseconds.
 * @param total_ms Total track duration in milliseconds.
 */
void ui_update_progress(uint32_t current_ms, uint32_t total_ms);

/**
 * @brief Update lyrics dataset and refresh the 5-line scrolling window.
 *
 * @param lyrics Pointer to parsed lyrics data, or NULL to clear lyrics.
 */
void ui_update_lyrics(const LyricsData *lyrics);

/**
 * @brief Update Wi-Fi connection indicator status.
 *
 * @param connected true if connected to Wi-Fi, false otherwise.
 */
void ui_set_wifi_status(bool connected);

/**
 * @brief Switch UI to AP setup / Captive Portal configuration mode.
 *
 * @param ap_mode true to show setup mode instructions, false for normal mode.
 * @param ap_ssid Optional hotspot SSID string (defaults to "ESP32-Lyrics-Setup").
 */
void ui_set_ap_setup_mode(bool ap_mode, const char *ap_ssid);

/**
 * @brief Get the read-only snapshot of current UI state.
 *
 * @return Constant pointer to internal UIState structure.
 */
const UIState* ui_get_state(void);

// =============================================================================
// Pure Formatting and Window Helpers (Native Host & Embedded Target)
// =============================================================================

/**
 * @brief Format playback milliseconds into human-readable string ("mm:ss" or "hh:mm:ss").
 *
 * @param ms Playback time in milliseconds.
 * @param buf Output string buffer.
 * @param buf_len Total buffer capacity including null-terminator.
 */
void format_time_ms(uint32_t ms, char *buf, size_t buf_len);

/**
 * @brief Truncate string with an ellipsis ("...") if it exceeds max_len - 1.
 *
 * @param src Source null-terminated string.
 * @param dst Destination buffer.
 * @param max_len Buffer size including null-terminator.
 */
void truncate_for_display(const char *src, char *dst, size_t max_len);

/**
 * @brief Compute the 5-line lyrics scrolling window for a given track time.
 *
 * @param lyrics Parsed lyrics data pointer.
 * @param current_ms Playback position in milliseconds.
 * @param window 2D char array to receive 5 lines [-2, -1, 0, +1, +2].
 * @param active_index Pointer to receive active line index (-1 if none).
 */
void compute_lyric_window(const LyricsData *lyrics, uint32_t current_ms, char window[5][128], int *active_index);

#ifdef __cplusplus
}
#endif

#endif // UI_DISPLAY_H
