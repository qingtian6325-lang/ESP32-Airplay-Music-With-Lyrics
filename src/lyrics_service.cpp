#include "lyrics_service.h"
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
                    while (p < line_end && (*p == ' ' || *p == '\t')) p++;
                    if (p >= line_end || *p != '[') break;

                    uint32_t ts = 0;
                    int tag_len = 0;
                    if (parse_timestamp_tag(p, &ts, &tag_len)) {
                        if (num_ts < 32) {
                            timestamps[num_ts++] = ts;
                        }
                        p += tag_len;
                    } else {
                        break;
                    }
                }

                if (num_ts > 0) {
                    // Skip spaces after tags before text
                    while (p < line_end && (*p == ' ' || *p == '\t')) p++;

                    const char *text_end = line_end;
                    // Trim trailing \r, \n, spaces, and tabs
                    while (text_end > p && (*(text_end - 1) == '\r' || *(text_end - 1) == '\n' ||
                                            *(text_end - 1) == ' ' || *(text_end - 1) == '\t')) {
                        text_end--;
                    }

                    size_t text_len = text_end - p;
                    if (text_len >= MAX_LRC_LINE_LEN) {
                        text_len = MAX_LRC_LINE_LEN - 1;
                    }

                    for (size_t i = 0; i < num_ts; i++) {
                        if (out->count < MAX_LRC_LINES) {
                            LyricLine *l = &out->lines[out->count++];
                            l->time_ms = timestamps[i];
                            if (text_len > 0) {
                                memcpy(l->text, p, text_len);
                            }
                            l->text[text_len] = '\0';
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

    if (out->count == 0) {
        return false;
    }

    qsort(out->lines, out->count, sizeof(LyricLine), compare_lyric_lines);
    out->loaded = true;
    return true;
}

int lrc_get_active_index_at(const LyricsData *data, uint32_t ms) {
    if (!data || !data->loaded || data->count == 0) {
        return -1;
    }

    if (ms < data->lines[0].time_ms) {
        return -1;
    }

    if (ms >= data->lines[data->count - 1].time_ms) {
        return (int)(data->count - 1);
    }

    int low = 0;
    int high = (int)data->count - 1;
    int best = 0;

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
    if (!dst || dst_len == 0) return;
    dst[0] = '\0';
    if (!src) return;

    size_t out_idx = 0;
    static const char hex[] = "0123456789ABCDEF";

    while (*src != '\0' && out_idx + 1 < dst_len) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[out_idx++] = c;
        } else if (c == ' ') {
            if (out_idx + 3 < dst_len) {
                dst[out_idx++] = '%';
                dst[out_idx++] = '2';
                dst[out_idx++] = '0';
            } else {
                break;
            }
        } else {
            if (out_idx + 3 < dst_len) {
                dst[out_idx++] = '%';
                dst[out_idx++] = hex[(c >> 4) & 0x0F];
                dst[out_idx++] = hex[c & 0x0F];
            } else {
                break;
            }
        }
        src++;
    }
    dst[out_idx] = '\0';
}

// =============================================================================
// FreeRTOS Asynchronous HTTP Client Implementation (ESP32-WROVER-E on CORE_UI)
// =============================================================================

#if (defined(ESP_PLATFORM) || defined(ARDUINO)) && !defined(ARDUINO_ARCH_NATIVE)

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "config.h"

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

            char url[600];
            snprintf(url, sizeof(url), "https://lrclib.net/api/get?artist_name=%s&track_name=%s",
                     enc_artist, enc_title);

            WiFiClientSecure client;
            client.setInsecure(); // Skip certificate verification for performance
            HTTPClient http;
            http.begin(client, url);
            http.setUserAgent("ESP32-Lyrics-Speaker/1.0");
            http.setTimeout(5000);

            int httpCode = http.GET();
            bool parsed_ok = false;

            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, payload);
                if (!err) {
                    const char *synced_lyrics = doc["syncedLyrics"];
                    if (synced_lyrics && strlen(synced_lyrics) > 0) {
                        if (lrc_parse(synced_lyrics, s_worker_lyrics_data)) {
                            parsed_ok = true;
                        }
                    }
                }
            }
            http.end();

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
static bool s_mock_success = false;
static char s_mock_raw_lrc[16384] = {0};
static LyricsData s_mock_lyrics_data;

bool lyrics_service_init(void) {
    s_mock_initialized = true;
    return true;
}

void lyrics_service_stop(void) {
    s_mock_initialized = false;
    lyrics_mock_reset();
}

void lyrics_fetch_async(const char *title, const char *artist, LyricsLoadedCallback cb) {
    memset(&s_mock_lyrics_data, 0, sizeof(s_mock_lyrics_data));
    if (title) strncpy(s_mock_lyrics_data.title, title, sizeof(s_mock_lyrics_data.title) - 1);
    if (artist) strncpy(s_mock_lyrics_data.artist, artist, sizeof(s_mock_lyrics_data.artist) - 1);

    if (!s_mock_initialized || !s_mock_success) {
        s_mock_lyrics_data.loaded = false;
        if (cb) {
            cb(&s_mock_lyrics_data, false);
        }
        return;
    }

    bool parsed = lrc_parse(s_mock_raw_lrc, &s_mock_lyrics_data);
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

void lyrics_mock_set_response(const char *raw_lrc, bool success) {
    s_mock_success = success;
    if (raw_lrc) {
        strncpy(s_mock_raw_lrc, raw_lrc, sizeof(s_mock_raw_lrc) - 1);
        s_mock_raw_lrc[sizeof(s_mock_raw_lrc) - 1] = '\0';
    } else {
        s_mock_raw_lrc[0] = '\0';
    }
}

void lyrics_mock_reset(void) {
    s_mock_success = false;
    s_mock_raw_lrc[0] = '\0';
    memset(&s_mock_lyrics_data, 0, sizeof(s_mock_lyrics_data));
}

#endif
