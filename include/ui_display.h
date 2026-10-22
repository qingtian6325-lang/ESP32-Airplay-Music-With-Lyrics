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
 * @param out Output string buffer.
 * @param out_len Capacity of out buffer including null terminator.
 */
void format_time_ms(uint32_t ms, char *out, size_t out_len);

/**
 * @brief Truncate string for display screen width with ellipsis protection.
 *
 * @param in Input string.
 * @param out Output string buffer.
 * @param max_len Maximum length of out buffer including null terminator.
 */
void truncate_for_display(const char *in, char *out, size_t max_len);

/**
 * @brief Compute the 5-line lyric window [-2, -1, 0 (active), +1, +2].
 *
 * @param lyrics Pointer to loaded lyrics data.
 * @param current_ms Playback position in milliseconds.
 * @param window Output 5x128 2D character array for the 5 lines.
 * @param active_idx Output pointer for active lyric line index (-1 if none/before start).
 */
void compute_lyric_window(const LyricsData *lyrics, uint32_t current_ms, char window[5][128], int *active_idx);

#ifdef __cplusplus
}
#endif

#endif // UI_DISPLAY_H
