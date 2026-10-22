#include "lyrics_service.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// =============================================================================
// Pure LRC Parser & Lookup Implementation (Native Host & Embedded Target)
// =============================================================================

static int compare_lyric_lines(const void *a, const void *b) {
    const LyricLine *la = (const LyricLine *)a;
    const LyricLine *lb = (const LyricLine *)b;
    if (la->time_ms < lb->time_ms) return -1;
    if (la->time_ms > lb->time_ms) return 1;
    return 0;
}

/**
 * @brief Parse a single timestamp tag e.g. "[mm:ss.xx]", "[mm:ss.xxx]", or "[hh:mm:ss.xx]".
 */
static bool parse_timestamp_tag(const char *p, uint32_t *out_ms, int *out_len) {
    if (!p || *p != '[' || !out_ms || !out_len) {
        return false;
    }

    const char *tag_start = p;
    p++; // skip '['

    if (!isdigit((unsigned char)*p)) {
        return false;
    }

    uint32_t val1 = 0;
    while (isdigit((unsigned char)*p)) {
        val1 = val1 * 10 + (*p - '0');
        p++;
    }

    if (*p != ':') {
        return false;
    }
    p++; // skip ':'

    if (!isdigit((unsigned char)*p)) {
        return false;
    }

    uint32_t val2 = 0;
    while (isdigit((unsigned char)*p)) {
        val2 = val2 * 10 + (*p - '0');
        p++;
    }

    uint32_t mins = 0;
    uint32_t secs = 0;

    if (*p == ':') {
        p++; // second colon -> val1 was hours, val2 is mins, next is secs
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        uint32_t val3 = 0;
        while (isdigit((unsigned char)*p)) {
            val3 = val3 * 10 + (*p - '0');
            p++;
        }
        mins = val1 * 60 + val2;
        secs = val3;
    } else {
        mins = val1;
        secs = val2;
    }

    uint32_t frac_ms = 0;
    if (*p == '.' || *p == ':') {
        p++; // skip decimal point or colon
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        int frac_digits = 0;
        uint32_t frac_val = 0;
        while (isdigit((unsigned char)*p)) {
            if (frac_digits < 3) {
                frac_val = frac_val * 10 + (*p - '0');
                frac_digits++;
            }
            p++;
        }

        if (frac_digits == 1) {
            frac_ms = frac_val * 100;
        } else if (frac_digits == 2) {
            frac_ms = frac_val * 10;
        } else if (frac_digits == 3) {
            frac_ms = frac_val;
        }
    }

    if (*p != ']') {
        return false;
    }
    p++; // skip ']'

    *out_ms = mins * 60000 + secs * 1000 + frac_ms;
    *out_len = (int)(p - tag_start);
    return true;
}

bool lrc_parse(const char *raw_lrc, LyricsData *out) {
    if (!raw_lrc || !out) {
        return false;
    }

    out->count = 0;
    out->loaded = false;
    out->title[0] = '\0';
    out->artist[0] = '\0';

    const char *line_start = raw_lrc;
    while (*line_start != '\0') {
        // Find line end
        const char *line_end = line_start;
        while (*line_end != '\0' && *line_end != '\n') {
            line_end++;
        }

        const char *p = line_start;
        while (p < line_end && (*p == ' ' || *p == '\t')) {
            p++;
        }

        if (p < line_end) {
            // Check for metadata tags if title or artist are not set
            if (strncmp(p, "[ti:", 4) == 0) {
                const char *val_start = p + 4;
                const char *val_end = val_start;
                while (val_end < line_end && *val_end != ']') val_end++;
                if (val_end < line_end && out->title[0] == '\0') {
                    size_t len = val_end - val_start;
                    if (len >= sizeof(out->title)) len = sizeof(out->title) - 1;
                    memcpy(out->title, val_start, len);
                    out->title[len] = '\0';
                }
            } else if (strncmp(p, "[ar:", 4) == 0) {
                const char *val_start = p + 4;
                const char *val_end = val_start;
                while (val_end < line_end && *val_end != ']') val_end++;
                if (val_end < line_end && out->artist[0] == '\0') {
                    size_t len = val_end - val_start;
                    if (len >= sizeof(out->artist)) len = sizeof(out->artist) - 1;
                    memcpy(out->artist, val_start, len);
                    out->artist[len] = '\0';
                }
            } else {
                // Scan consecutive timestamp tags at the beginning of the line
                uint32_t timestamps[32];
                size_t num_ts = 0;

                while (p < line_end) {
                    uint32_t ts_ms = 0;
                    int tag_len = 0;
                    if (parse_timestamp_tag(p, &ts_ms, &tag_len)) {
                        if (num_ts < sizeof(timestamps) / sizeof(timestamps[0])) {
                            timestamps[num_ts++] = ts_ms;
                        }
                        p += tag_len;
                    } else {
                        break;
                    }
                }

                if (num_ts > 0) {
                    // Skip leading whitespace on the text portion
                    while (p < line_end && (*p == ' ' || *p == '\t')) {
                        p++;
                    }

                    // Trim trailing CR / whitespace
                    const char *text_end = line_end;
                    while (text_end > p && (*(text_end - 1) == '\r' || *(text_end - 1) == ' ' || *(text_end - 1) == '\t')) {
                        text_end--;
                    }

                    size_t text_len = text_end - p;
                    char line_buf[MAX_LRC_LINE_LEN];
                    if (text_len >= sizeof(line_buf)) {
                        text_len = sizeof(line_buf) - 1;
                    }
                    if (text_len > 0) {
                        memcpy(line_buf, p, text_len);
                    }
                    line_buf[text_len] = '\0';

                    // Insert lyric lines for each timestamp parsed
                    for (size_t i = 0; i < num_ts; i++) {
                        if (out->count < MAX_LRC_LINES) {
                            out->lines[out->count].time_ms = timestamps[i];
                            strncpy(out->lines[out->count].text, line_buf, sizeof(out->lines[out->count].text) - 1);
                            out->lines[out->count].text[sizeof(out->lines[out->count].text) - 1] = '\0';
                            out->count++;
                        }
                    }
                }
            }
        }

        if (*line_end == '\n') {
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    if (out->count > 0) {
        qsort(out->lines, out->count, sizeof(LyricLine), compare_lyric_lines);
        out->loaded = true;
        return true;
    }

    return false;
}

int lrc_get_active_index_at(const LyricsData *data, uint32_t ms) {
    if (!data || !data->loaded || data->count == 0) {
        return -1;
    }

    if (ms >= data->lines[data->count - 1].time_ms) {
        return (int)(data->count - 1);
    }

    int low = 0;
    int high = (int)data->count - 1;
    int best = -1;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (data->lines[mid].time_ms <= ms) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return best;
}

const char* lrc_get_line_at(const LyricsData *data, uint32_t ms, int offset_line) {
    if (!data || !data->loaded || data->count == 0) {
        return "";
    }

    int active_idx = lrc_get_active_index_at(data, ms);
    int target_idx = active_idx + offset_line;

    if (target_idx < 0 || target_idx >= (int)data->count) {
        return "";
    }

    return data->lines[target_idx].text;
}

void url_encode(const char *src, char *dst, size_t dst_len) {
    if (!src || !dst || dst_len == 0) {
        if (dst && dst_len > 0) dst[0] = '\0';
        return;
    }

    static const char *hex = "0123456789ABCDEF";
    size_t written = 0;

    while (*src != '\0' && written + 1 < dst_len) {
        unsigned char c = (unsigned char)*src;
        // Unreserved characters: A-Z, a-z, 0-9, '-', '_', '.', '~'
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[written++] = c;
        } else {
            if (written + 4 > dst_len) {
                break; // Not enough room for %XX + null terminator
            }
            dst[written++] = '%';
            dst[written++] = hex[(c >> 4) & 0x0F];
            dst[written++] = hex[c & 0x0F];
        }
        src++;
    }

    dst[written] = '\0';
}

// =============================================================================
// Platform-Specific Implementation (FreeRTOS / Core 1 vs Host Mock)
// =============================================================================

#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

typedef struct {
    char title[128];
    char artist[128];
    LyricsLoadedCallback callback;
} LyricsRequest;

static QueueHandle_t s_lyrics_queue = NULL;
static TaskHandle_t s_lyrics_task_handle = NULL;
static volatile bool s_service_running = false;

// Dynamically allocated in PSRAM / heap to prevent .dram0.bss overflow
static LyricsData *s_worker_lyrics_data = NULL;

static bool query_lrccx_provider(const char *enc_title, const char *enc_artist, LyricsData *out) {
    char url[600];
    if (enc_artist && enc_artist[0] != '\0') {
        snprintf(url, sizeof(url), "%s?title=%s&artist=%s", LRC_CX_API_ENDPOINT, enc_title, enc_artist);
    } else {
        snprintf(url, sizeof(url), "%s?title=%s", LRC_CX_API_ENDPOINT, enc_title);
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setUserAgent("ESP32-Lyrics-Speaker/1.0");
    http.setTimeout(4000);

    int httpCode = http.GET();
    bool ok = false;
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        if (payload.length() > 0) {
            if (lrc_parse(payload.c_str(), out)) {
                ok = true;
            }
        }
    }
    http.end();
    return ok;
}

static bool query_lrclib_provider(const char *enc_title, const char *enc_artist, LyricsData *out) {
    char url[600];
    snprintf(url, sizeof(url), "%s?track_name=%s&artist_name=%s", LRCLIB_API_ENDPOINT, enc_title, enc_artist);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setUserAgent("ESP32-Lyrics-Speaker/1.0");
    http.setTimeout(4000);

    int httpCode = http.GET();
    bool ok = false;
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
            const char *synced_lyrics = doc["syncedLyrics"];
            if (synced_lyrics && strlen(synced_lyrics) > 0) {
                if (lrc_parse(synced_lyrics, out)) {
                    ok = true;
                }
            }
        }
    }
    http.end();
    return ok;
}

static void lyrics_worker_task(void *pvParameters) {
    (void)pvParameters;

    while (s_service_running) {
        LyricsRequest req;
        if (xQueueReceive(s_lyrics_queue, &req, portMAX_DELAY) == pdTRUE) {
            if (!s_service_running) {
                break;
            }

            if (!s_worker_lyrics_data) {
                continue;
            }
            memset(s_worker_lyrics_data, 0, sizeof(LyricsData));
            // Do not pre-populate title and artist so lrc_parse can extract [ti:] and [ar:] from tags.

            if (WiFi.status() != WL_CONNECTED) {
                if (req.callback) {
                    if (req.title[0] != '\0') strncpy(s_worker_lyrics_data->title, req.title, sizeof(s_worker_lyrics_data->title) - 1);
                    if (req.artist[0] != '\0') strncpy(s_worker_lyrics_data->artist, req.artist, sizeof(s_worker_lyrics_data->artist) - 1);
                    req.callback(s_worker_lyrics_data, false);
                }
                continue;
            }

            char enc_artist[256];
            char enc_title[256];
            url_encode(req.artist, enc_artist, sizeof(enc_artist));
            url_encode(req.title, enc_title, sizeof(enc_title));

            bool parsed_ok = false;
            int primary = LYRICS_DEFAULT_PROVIDER;
            int secondary = (primary == LYRICS_PROVIDER_LRCCX) ? LYRICS_PROVIDER_LRCLIB : LYRICS_PROVIDER_LRCCX;

            // 1. Query preferred primary provider
            if (primary == LYRICS_PROVIDER_LRCCX) {
                parsed_ok = query_lrccx_provider(enc_title, enc_artist, s_worker_lyrics_data);
            } else {
                parsed_ok = query_lrclib_provider(enc_title, enc_artist, s_worker_lyrics_data);
            }

            // 2. Cascade fallback to secondary provider if primary failed
            if (!parsed_ok && LYRICS_ENABLE_FALLBACK) {
                if (secondary == LYRICS_PROVIDER_LRCCX) {
                    parsed_ok = query_lrccx_provider(enc_title, enc_artist, s_worker_lyrics_data);
                } else {
                    parsed_ok = query_lrclib_provider(enc_title, enc_artist, s_worker_lyrics_data);
                }
            }

            // Fallback to request title/artist if not present in LRC metadata tags
            if (s_worker_lyrics_data->title[0] == '\0') {
                strncpy(s_worker_lyrics_data->title, req.title, sizeof(s_worker_lyrics_data->title) - 1);
            }
            if (s_worker_lyrics_data->artist[0] == '\0') {
                strncpy(s_worker_lyrics_data->artist, req.artist, sizeof(s_worker_lyrics_data->artist) - 1);
            }

            if (req.callback) {
                req.callback(s_worker_lyrics_data, parsed_ok);
            }
        }
    }

    s_lyrics_task_handle = NULL;
    vTaskDelete(NULL);
}

bool lyrics_service_init(void) {
    if (!s_worker_lyrics_data) {
        if (psramFound()) {
            s_worker_lyrics_data = (LyricsData *)ps_malloc(sizeof(LyricsData));
        }
        if (!s_worker_lyrics_data) {
            s_worker_lyrics_data = (LyricsData *)malloc(sizeof(LyricsData));
        }
        if (!s_worker_lyrics_data) {
            return false;
        }
    }

    if (s_service_running) {
        return true;
    }

    if (!s_lyrics_queue) {
        s_lyrics_queue = xQueueCreate(4, sizeof(LyricsRequest));
        if (!s_lyrics_queue) {
            return false;
        }
    }

    s_service_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        lyrics_worker_task,
        "lyrics_worker",
        8192,
        NULL,
        1,
        &s_lyrics_task_handle,
        CORE_UI
    );

    if (ret != pdPASS) {
        s_service_running = false;
        vQueueDelete(s_lyrics_queue);
        s_lyrics_queue = NULL;
        return false;
    }

    return true;
}

void lyrics_service_stop(void) {
    if (!s_service_running) {
        return;
    }

    s_service_running = false;

    // Wake worker task if waiting on queue
    if (s_lyrics_queue) {
        LyricsRequest wake_req;
        memset(&wake_req, 0, sizeof(wake_req));
        xQueueSend(s_lyrics_queue, &wake_req, 0);
    }

    // Wait gracefully for worker task to terminate cleanly
    if (s_lyrics_task_handle != NULL) {
        for (int i = 0; i < 30 && s_lyrics_task_handle != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_lyrics_task_handle != NULL) {
            vTaskDelete(s_lyrics_task_handle);
            s_lyrics_task_handle = NULL;
        }
    }

    if (s_lyrics_queue) {
        vQueueDelete(s_lyrics_queue);
        s_lyrics_queue = NULL;
    }
}

void lyrics_fetch_async(const char *title, const char *artist, LyricsLoadedCallback cb) {
    if (!s_service_running || !s_lyrics_queue) {
        if (cb) {
            if (s_worker_lyrics_data) {
                memset(s_worker_lyrics_data, 0, sizeof(LyricsData));
                if (title) strncpy(s_worker_lyrics_data->title, title, sizeof(s_worker_lyrics_data->title) - 1);
                if (artist) strncpy(s_worker_lyrics_data->artist, artist, sizeof(s_worker_lyrics_data->artist) - 1);
                s_worker_lyrics_data->loaded = false;
                cb(s_worker_lyrics_data, false);
            } else {
                cb(NULL, false);
            }
        }
        return;
    }

    LyricsRequest req;
    memset(&req, 0, sizeof(req));
    if (title) strncpy(req.title, title, sizeof(req.title) - 1);
    if (artist) strncpy(req.artist, artist, sizeof(req.artist) - 1);
    req.callback = cb;

    // If queue is full, drain oldest request so newest track request has priority
    if (uxQueueSpacesAvailable(s_lyrics_queue) == 0) {
        LyricsRequest dropped;
        xQueueReceive(s_lyrics_queue, &dropped, 0);
    }

    xQueueSend(s_lyrics_queue, &req, 0);
}

#else

// =============================================================================
// Native / Host Mock Implementation for Unit Tests
// =============================================================================

static bool s_mock_initialized = false;
static bool s_mock_lrccx_success = false;
static char s_mock_lrccx_raw_lrc[16384] = {0};

static bool s_mock_lrclib_success = false;
static char s_mock_lrclib_raw_lrc[16384] = {0};

static int s_mock_last_provider = 0;
static LyricsData s_mock_lyrics_data;

bool lyrics_service_init(void) {
    s_mock_initialized = true;
    return true;
}

void lyrics_service_stop(void) {
    s_mock_initialized = false;
    lyrics_mock_reset();
}

int lyrics_mock_get_last_queried_provider(void) {
    return s_mock_last_provider;
}

void lyrics_mock_set_response(const char *raw_lrc, bool success) {
    // Default legacy setter sets primary provider (lrc.cx)
    lyrics_mock_set_provider_response(LYRICS_DEFAULT_PROVIDER, raw_lrc, success);
}

void lyrics_mock_set_provider_response(int provider, const char *raw_lrc, bool success) {
    if (provider == LYRICS_PROVIDER_LRCCX) {
        s_mock_lrccx_success = success;
        if (raw_lrc) {
            strncpy(s_mock_lrccx_raw_lrc, raw_lrc, sizeof(s_mock_lrccx_raw_lrc) - 1);
            s_mock_lrccx_raw_lrc[sizeof(s_mock_lrccx_raw_lrc) - 1] = '\0';
        } else {
            s_mock_lrccx_raw_lrc[0] = '\0';
        }
    } else if (provider == LYRICS_PROVIDER_LRCLIB) {
        s_mock_lrclib_success = success;
        if (raw_lrc) {
            strncpy(s_mock_lrclib_raw_lrc, raw_lrc, sizeof(s_mock_lrclib_raw_lrc) - 1);
            s_mock_lrclib_raw_lrc[sizeof(s_mock_lrclib_raw_lrc) - 1] = '\0';
        } else {
            s_mock_lrclib_raw_lrc[0] = '\0';
        }
    }
}

void lyrics_mock_reset(void) {
    s_mock_lrccx_success = false;
    s_mock_lrccx_raw_lrc[0] = '\0';
    s_mock_lrclib_success = false;
    s_mock_lrclib_raw_lrc[0] = '\0';
    s_mock_last_provider = 0;
    memset(&s_mock_lyrics_data, 0, sizeof(s_mock_lyrics_data));
}

void lyrics_fetch_async(const char *title, const char *artist, LyricsLoadedCallback cb) {
    memset(&s_mock_lyrics_data, 0, sizeof(s_mock_lyrics_data));
    if (title) strncpy(s_mock_lyrics_data.title, title, sizeof(s_mock_lyrics_data.title) - 1);
    if (artist) strncpy(s_mock_lyrics_data.artist, artist, sizeof(s_mock_lyrics_data.artist) - 1);

    if (!s_mock_initialized) {
        s_mock_lyrics_data.loaded = false;
        if (cb) {
            cb(&s_mock_lyrics_data, false);
        }
        return;
    }

    int primary = LYRICS_DEFAULT_PROVIDER;
    int secondary = (primary == LYRICS_PROVIDER_LRCCX) ? LYRICS_PROVIDER_LRCLIB : LYRICS_PROVIDER_LRCCX;

    bool parsed = false;

    // 1. Query primary provider
    s_mock_last_provider = primary;
    bool primary_ok = (primary == LYRICS_PROVIDER_LRCCX) ? s_mock_lrccx_success : s_mock_lrclib_success;
    const char *primary_lrc = (primary == LYRICS_PROVIDER_LRCCX) ? s_mock_lrccx_raw_lrc : s_mock_lrclib_raw_lrc;

    if (primary_ok && primary_lrc && primary_lrc[0] != '\0') {
        parsed = lrc_parse(primary_lrc, &s_mock_lyrics_data);
    }

    // 2. Cascade fallback to secondary provider if primary failed
    if (!parsed && LYRICS_ENABLE_FALLBACK) {
        s_mock_last_provider = secondary;
        bool secondary_ok = (secondary == LYRICS_PROVIDER_LRCCX) ? s_mock_lrccx_success : s_mock_lrclib_success;
        const char *secondary_lrc = (secondary == LYRICS_PROVIDER_LRCCX) ? s_mock_lrccx_raw_lrc : s_mock_lrclib_raw_lrc;

        if (secondary_ok && secondary_lrc && secondary_lrc[0] != '\0') {
            parsed = lrc_parse(secondary_lrc, &s_mock_lyrics_data);
        }
    }

    // Retain title/artist if not specified in LRC metadata headers
    if (s_mock_lyrics_data.title[0] == '\0' && title) {
        strncpy(s_mock_lyrics_data.title, title, sizeof(s_mock_lyrics_data.title) - 1);
    }
    if (s_mock_lyrics_data.artist[0] == '\0' && artist) {
        strncpy(s_mock_lyrics_data.artist, artist, sizeof(s_mock_lyrics_data.artist) - 1);
    }

    if (cb) {
        cb(&s_mock_lyrics_data, parsed);
    }
}

#endif
