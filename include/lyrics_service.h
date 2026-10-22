#ifndef LYRICS_SERVICE_H
#define LYRICS_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LRC_LINES 250
#define MAX_LRC_LINE_LEN 128

typedef struct LyricLine {
    uint32_t time_ms;
    char text[MAX_LRC_LINE_LEN];
} LyricLine;

typedef struct LyricsData {
    LyricLine lines[MAX_LRC_LINES];
    size_t count;
    char title[128];
    char artist[128];
    bool loaded;
} LyricsData;

typedef void (*LyricsLoadedCallback)(const LyricsData *lyrics, bool success);

/**
 * @brief Parse raw LRC text into a structured, timestamp-sorted LyricsData instance.
 *
 * Handles [mm:ss.xx] and [mm:ss.xxx] formats, multi-timestamps per line,
 * sorts lines chronologically ascending, and ignores non-timestamp tags.
 *
 * @param raw_lrc Null-terminated LRC string.
 * @param out Output LyricsData pointer.
 * @return true if at least one timestamped line was parsed successfully, false otherwise.
 */
bool lrc_parse(const char *raw_lrc, LyricsData *out);

/**
 * @brief Binary search for active lyric line index at a given playback millisecond.
 *
 * @param data Parsed lyrics data pointer.
 * @param ms Current track playback position in milliseconds.
 * @return Active line index (0 to count-1), or -1 if ms is before the first line or on error.
 */
int lrc_get_active_index_at(const LyricsData *data, uint32_t ms);

/**
 * @brief Get the lyric line string at a given playback millisecond with a relative offset.
 *
 * Offset 0 is the current active line.
 * Offset -1 is the previous line, +1 is the upcoming line.
 * Returns an empty string ("") if the requested line is out of bounds or data is invalid.
 *
 * @param data Parsed lyrics data pointer.
 * @param ms Current track playback position in milliseconds.
 * @param offset_line Relative line offset (-2 to +2, etc.).
 * @return Constant string pointer to line text (never returns NULL).
 */
const char* lrc_get_line_at(const LyricsData *data, uint32_t ms, int offset_line);

/**
 * @brief Pure URL encoding helper (RFC 3986 percent-encoding).
 *
 * @param src Source null-terminated string.
 * @param dst Output buffer.
 * @param dst_len Total capacity of dst buffer including null terminator.
 */
void url_encode(const char *src, char *dst, size_t dst_len);

/**
 * @brief Initialize the asynchronous lyrics service worker on Core 1 (CORE_UI).
 *
 * @return true if initialized successfully, false otherwise.
 */
bool lyrics_service_init(void);

/**
 * @brief Stop the lyrics service worker and release resources.
 */
void lyrics_service_stop(void);

/**
 * @brief Request lyrics for the specified track asynchronously.
 *
 * Queries LRCLIB API on Core 1 (CORE_UI) without blocking Core 0 audio playback.
 * Invokes callback upon completion or failure.
 *
 * @param title Track title.
 * @param artist Artist name.
 * @param cb Completion callback.
 */
void lyrics_fetch_async(const char *title, const char *artist, LyricsLoadedCallback cb);

#if !(defined(ESP_PLATFORM) || defined(ARDUINO)) || defined(ARDUINO_ARCH_NATIVE)
/**
 * @brief Host mock helpers for testing without ESP32 hardware or network.
 */
void lyrics_mock_set_response(const char *raw_lrc, bool success);
void lyrics_mock_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // LYRICS_SERVICE_H
