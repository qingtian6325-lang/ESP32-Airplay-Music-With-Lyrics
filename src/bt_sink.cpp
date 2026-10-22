#include "bt_sink.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static void sanitize_string(const char *src, char *dest, size_t dest_size) {
    if (!dest || dest_size == 0) {
        return;
    }
    dest[0] = '\0';
    if (!src) {
        return;
    }

    // Skip leading whitespace
    const char *start = src;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (*start == '\0') {
        return;
    }

    // Find trailing non-whitespace
    const char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }

    size_t len = (size_t)(end - start + 1);
    if (len >= dest_size) {
        len = dest_size - 1;
    }

    memcpy(dest, start, len);
    dest[len] = '\0';
}

void parse_avrcp_metadata(const char *raw_title, const char *raw_artist, const char *raw_album, TrackInfo *out) {
    if (!out) {
        return;
    }
    sanitize_string(raw_title, out->title, sizeof(out->title));
    sanitize_string(raw_artist, out->artist, sizeof(out->artist));
    sanitize_string(raw_album, out->album, sizeof(out->album));
    out->duration_ms = 0;
}

#if (defined(ESP_PLATFORM) || defined(ARDUINO)) && !defined(ARDUINO_ARCH_NATIVE)

#include <Arduino.h>
#include "BluetoothA2DPSink.h"
#include "esp_avrc_api.h"
#include "esp_a2dp_api.h"
#include "audio_i2s.h"

static BluetoothA2DPSink s_a2dp_sink;
static TrackChangeCallback s_track_callback = nullptr;
static PlaybackStatusCallback s_status_callback = nullptr;

static TrackInfo s_current_track = { {0}, {0}, {0}, 0 };
static TrackInfo s_last_notified_track = { {0}, {0}, {0}, 0 };
static bool s_is_playing = false;

// Audio data callback: forwards decoded stereo PCM frames directly to audio_i2s_write
static void bt_audio_data_callback(const uint8_t *data, uint32_t len) {
    if (data && len > 0) {
        audio_i2s_write(data, (size_t)len);
    }
}

// Check and notify track change if title, artist, or album has changed
static void check_and_notify_track_change() {
    if (!s_track_callback) {
        return;
    }
    // Only notify if we have at least a non-empty title or artist
    if (s_current_track.title[0] == '\0' && s_current_track.artist[0] == '\0') {
        return;
    }
    if (strcmp(s_current_track.title, s_last_notified_track.title) != 0 ||
        strcmp(s_current_track.artist, s_last_notified_track.artist) != 0 ||
        strcmp(s_current_track.album, s_last_notified_track.album) != 0) {
        
        s_last_notified_track = s_current_track;
        s_track_callback(&s_current_track);
    }
}

// AVRCP Metadata Callback (called per attribute received from remote)
static void bt_avrc_metadata_callback(uint8_t attr_id, const uint8_t *attr_text) {
    const char *text = (const char *)attr_text;
    switch (attr_id) {
        case ESP_AVRC_MD_ATTR_TITLE:
            sanitize_string(text, s_current_track.title, sizeof(s_current_track.title));
            check_and_notify_track_change();
            break;
        case ESP_AVRC_MD_ATTR_ARTIST:
            sanitize_string(text, s_current_track.artist, sizeof(s_current_track.artist));
            check_and_notify_track_change();
            break;
        case ESP_AVRC_MD_ATTR_ALBUM:
            sanitize_string(text, s_current_track.album, sizeof(s_current_track.album));
            check_and_notify_track_change();
            break;
        case ESP_AVRC_MD_ATTR_PLAYING_TIME:
            if (text && *text) {
                s_current_track.duration_ms = (uint32_t)strtoul(text, NULL, 10);
            }
            break;
        default:
            break;
    }
}

// AVRCP Track Change event (remote announced new track)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
static void bt_avrc_track_change_callback(uint8_t *id) {
    (void)id;
    // Reset last notified track so incoming metadata triggers notification
    memset(&s_last_notified_track, 0, sizeof(s_last_notified_track));
    memset(&s_current_track, 0, sizeof(s_current_track));
}
#endif

// AVRCP Play Status callback
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
static void bt_avrc_playstatus_callback(esp_avrc_playback_stat_t playback) {
    bool playing = (playback == ESP_AVRC_PLAYBACK_PLAYING);
    if (s_is_playing != playing) {
        s_is_playing = playing;
        if (s_status_callback) {
            s_status_callback(playing);
        }
    }
}
#endif

// A2DP Audio State callback (started / suspended / stopped)
static void bt_audio_state_callback(esp_a2d_audio_state_t state, void *obj) {
    (void)obj;
    bool playing = (state == ESP_A2D_AUDIO_STATE_STARTED);
    if (s_is_playing != playing) {
        s_is_playing = playing;
        if (s_status_callback) {
            s_status_callback(playing);
        }
    }
}

// A2DP Connection State callback
static void bt_connection_state_callback(esp_a2d_connection_state_t state, void *obj) {
    (void)obj;
    if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        s_is_playing = false;
        memset(&s_current_track, 0, sizeof(s_current_track));
        memset(&s_last_notified_track, 0, sizeof(s_last_notified_track));
        if (s_status_callback) {
            s_status_callback(false);
        }
    }
}

bool bt_sink_init(const char *device_name) {
    const char *name = (device_name && strlen(device_name) > 0) ? device_name : "ESP32-Lyrics-Speaker";

    // Set stream reader with is_i2s = false so ESP32-A2DP does not claim I2S pins or start its own I2S driver
    s_a2dp_sink.set_stream_reader(bt_audio_data_callback, false);

    // Register AVRCP callbacks
    s_a2dp_sink.set_avrc_metadata_callback(bt_avrc_metadata_callback);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
    s_a2dp_sink.set_avrc_rn_playstatus_callback(bt_avrc_playstatus_callback);
    s_a2dp_sink.set_avrc_rn_track_change_callback(bt_avrc_track_change_callback);
#endif

    // Register A2DP state callbacks
    s_a2dp_sink.set_on_connection_state_changed(bt_connection_state_callback);
    s_a2dp_sink.set_on_audio_state_changed(bt_audio_state_callback);

    // Request Title, Artist, Album, and Playing Time attributes
    s_a2dp_sink.set_avrc_metadata_attribute_mask(
        ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST |
        ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_PLAYING_TIME
    );

    s_a2dp_sink.start(name);
    return true;
}

void bt_sink_stop(void) {
    s_a2dp_sink.end(false);
    s_is_playing = false;
    memset(&s_current_track, 0, sizeof(s_current_track));
    memset(&s_last_notified_track, 0, sizeof(s_last_notified_track));
}

bool bt_sink_is_connected(void) {
    return s_a2dp_sink.is_connected();
}

bool bt_sink_is_playing(void) {
    return (s_a2dp_sink.get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED) || s_is_playing;
}

void bt_sink_register_track_callback(TrackChangeCallback cb) {
    s_track_callback = cb;
}

void bt_sink_register_status_callback(PlaybackStatusCallback cb) {
    s_status_callback = cb;
}

#else

// =============================================================================
// Native / Host Mock Implementation for Unit Tests
// =============================================================================

static bool s_mock_initialized = false;
static bool s_mock_connected = false;
static bool s_mock_playing = false;
static TrackChangeCallback s_mock_track_cb = nullptr;
static PlaybackStatusCallback s_mock_status_cb = nullptr;

bool bt_sink_init(const char *device_name) {
    (void)device_name;
    s_mock_initialized = true;
    return true;
}

void bt_sink_stop(void) {
    s_mock_initialized = false;
    s_mock_connected = false;
    s_mock_playing = false;
}

bool bt_sink_is_connected(void) {
    return s_mock_connected;
}

bool bt_sink_is_playing(void) {
    return s_mock_playing;
}

void bt_sink_register_track_callback(TrackChangeCallback cb) {
    s_mock_track_cb = cb;
}

void bt_sink_register_status_callback(PlaybackStatusCallback cb) {
    s_mock_status_cb = cb;
}

void bt_sink_mock_set_connected(bool connected) {
    s_mock_connected = connected;
    if (!connected) {
        s_mock_playing = false;
        if (s_mock_status_cb) {
            s_mock_status_cb(false);
        }
    }
}

void bt_sink_mock_set_playing(bool playing) {
    s_mock_playing = playing;
    if (s_mock_status_cb) {
        s_mock_status_cb(playing);
    }
}

void bt_sink_mock_trigger_track(const TrackInfo *track) {
    if (s_mock_track_cb && track) {
        s_mock_track_cb(track);
    }
}

#endif
