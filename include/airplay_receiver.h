#ifndef AIRPLAY_RECEIVER_H
#define AIRPLAY_RECEIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIRPLAY_METADATA_LEN 128

typedef void (*AirplayTrackCallback)(const char *title, const char *artist, const char *album);
typedef void (*AirplayStatusCallback)(bool is_active);

/**
 * @brief Initialize AirPlay 1 (RAOP) receiver, mDNS advertising, and RTSP server.
 * 
 * @param service_name Advertised AirPlay speaker name (defaults to "ESP32-Lyrics-Speaker" if null/empty).
 * @return true if initialized successfully, false otherwise.
 */
bool airplay_receiver_init(const char *service_name);

/**
 * @brief Stop AirPlay receiver, terminate active sessions, and release network resources.
 */
void airplay_receiver_stop(void);

/**
 * @brief Query if AirPlay audio session is currently active/streaming.
 * 
 * @return true if streaming/active, false otherwise.
 */
bool airplay_is_active(void);

/**
 * @brief Register callback for track metadata changes (title, artist, album).
 * 
 * @param cb Callback function receiving (title, artist, album).
 */
void airplay_register_track_callback(AirplayTrackCallback cb);

/**
 * @brief Register callback for AirPlay connection/playback status changes.
 * 
 * @param cb Callback function receiving boolean is_active.
 */
void airplay_register_status_callback(AirplayStatusCallback cb);

/**
 * @brief Pure function for parsing 4-character code (4CC) tags in DAAP/DACP metadata payloads.
 * 
 * Recursively inspects DAAP container tags, protects against buffer overflow,
 * and validates tag lengths.
 * 
 * @param payload Raw DAAP binary payload.
 * @param len Total length of payload in bytes.
 * @param tag_4cc 4-character code to search for (e.g., "minm", "asar", "asal").
 * @param out_val Output buffer for the extracted null-terminated string value.
 * @param out_val_len Capacity of out_val buffer including null terminator.
 * @return true if tag was found and parsed successfully, false otherwise.
 */
bool parse_daap_tag(const uint8_t *payload, size_t len, const char *tag_4cc, char *out_val, size_t out_val_len);

#if !(defined(ESP_PLATFORM) || defined(ARDUINO)) || defined(ARDUINO_ARCH_NATIVE)
/**
 * @brief Host mock helpers for unit testing AirPlay receiver without hardware/network.
 */
void airplay_mock_set_active(bool active);
void airplay_mock_trigger_track(const char *title, const char *artist, const char *album);
void airplay_mock_feed_daap_metadata(const uint8_t *payload, size_t len);
void airplay_mock_feed_audio(const uint8_t *pcm_data, size_t len);
void airplay_mock_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // AIRPLAY_RECEIVER_H
