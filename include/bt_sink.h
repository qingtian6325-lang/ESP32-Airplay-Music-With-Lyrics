#ifndef BT_SINK_H
#define BT_SINK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_METADATA_LEN 128

typedef struct {
    char title[128];
    char artist[128];
    char album[128];
    uint32_t duration_ms;
} TrackInfo;

typedef void (*TrackChangeCallback)(const TrackInfo *track);
typedef void (*PlaybackStatusCallback)(bool is_playing);

/**
 * @brief Initialize Bluetooth A2DP Sink with specified broadcast name.
 * 
 * @param device_name Bluetooth discoverable device name (defaults to "ESP32-Lyrics-Speaker" if null/empty).
 * @return true if initialized successfully, false otherwise.
 */
bool bt_sink_init(const char *device_name);

/**
 * @brief Stop Bluetooth A2DP Sink and release resources.
 */
void bt_sink_stop(void);

/**
 * @brief Query if Bluetooth A2DP source is currently connected.
 * 
 * @return true if connected, false otherwise.
 */
bool bt_sink_is_connected(void);

/**
 * @brief Query if Bluetooth audio is actively streaming/playing.
 * 
 * @return true if playing, false otherwise.
 */
bool bt_sink_is_playing(void);

/**
 * @brief Register callback for track metadata changes.
 * 
 * @param cb Callback function receiving updated TrackInfo pointer.
 */
void bt_sink_register_track_callback(TrackChangeCallback cb);

/**
 * @brief Register callback for playback status changes (playing/paused/stopped).
 * 
 * @param cb Callback function receiving boolean is_playing.
 */
void bt_sink_register_status_callback(PlaybackStatusCallback cb);

/**
 * @brief Pure helper to sanitize, trim whitespace, and parse AVRCP metadata strings into TrackInfo.
 * 
 * Trims leading/trailing whitespace, guards against NULL pointers, and truncates
 * to 127 chars + null terminator to prevent buffer overflows.
 * 
 * @param raw_title Raw title string from AVRCP (can be NULL).
 * @param raw_artist Raw artist string from AVRCP (can be NULL).
 * @param raw_album Raw album string from AVRCP (can be NULL).
 * @param out Pointer to output TrackInfo struct.
 */
void parse_avrcp_metadata(const char *raw_title, const char *raw_artist, const char *raw_album, TrackInfo *out);

#if !(defined(ESP_PLATFORM) || defined(ARDUINO)) || defined(ARDUINO_ARCH_NATIVE)
/**
 * @brief Native testing helpers for mocking BT state and triggering callbacks.
 */
void bt_sink_mock_set_connected(bool connected);
void bt_sink_mock_set_playing(bool playing);
void bt_sink_mock_trigger_track(const TrackInfo *track);
void bt_sink_mock_handle_avrc_attribute(uint8_t attr_id, const char *text);
void bt_sink_mock_flush_metadata(void);
#endif

#ifdef __cplusplus
}

// 3-parameter overload for convenient C++ usage (defaulting album to null)
inline void parse_avrcp_metadata(const char *raw_title, const char *raw_artist, TrackInfo *out) {
    parse_avrcp_metadata(raw_title, raw_artist, nullptr, out);
}
#endif

#endif // BT_SINK_H
