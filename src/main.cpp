#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"
#include "audio_i2s.h"
#include "bt_sink.h"
#include "airplay_receiver.h"
#include "lyrics_service.h"
#include "ui_display.h"
#include "state_arbiter.h"

// =============================================================================
// Default Wi-Fi Configuration (Configurable at build time or flash)
// =============================================================================
#ifndef WIFI_SSID
#define WIFI_SSID "ESP32-Speaker-AP"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#define SPEAKER_NAME "ESP32-Lyrics-Speaker"

// =============================================================================
// System State & Synchronization
// =============================================================================
static volatile SystemAudioState s_system_audio_state = STATE_IDLE;
static SemaphoreHandle_t s_state_mutex = NULL;

// Playback timing and progress interpolation variables (mutated exclusively under s_state_mutex)
static volatile uint32_t s_playback_start_ms = 0;
static volatile uint32_t s_track_duration_ms = 0;
static volatile bool s_is_playing = false;
static bool s_bt_stopped_for_airplay = false;

// Display tracking
static char s_current_title[128] = "Ready to Play";
static char s_current_artist[128] = "";

// =============================================================================
// Audio Source Arbitration Dispatcher
// =============================================================================
static void handle_system_audio_event(AudioEvent event) {
    if (s_state_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        SystemAudioState old_state = s_system_audio_state;
        SystemAudioState new_state = arbitrate_audio_event(old_state, event);

        if (old_state != new_state) {
            log_i("System Audio State: %s -> %s (Event: %s)",
                  audio_state_to_string(old_state),
                  audio_state_to_string(new_state),
                  audio_event_to_string(event));

            // AirPlay preemption over Bluetooth:
            // Stop Bluetooth A2DP audio so MAX98357A I2S is exclusively driven by AirPlay
            if (new_state == STATE_AIRPLAY_PLAYING) {
                if (old_state == STATE_BT_PLAYING || bt_sink_is_connected()) {
                    log_i("Entering AirPlay playback: stopping Bluetooth A2DP stream");
                    bt_sink_stop();
                    s_bt_stopped_for_airplay = true;
                }
                s_is_playing = true;
                s_playback_start_ms = millis();
            } else if (old_state == STATE_IDLE && new_state == STATE_BT_PLAYING) {
                s_is_playing = true;
                s_playback_start_ms = millis();
            } else if (new_state == STATE_IDLE) {
                // Both audio sources stopped / inactive -> return to IDLE state
                s_is_playing = false;
                s_track_duration_ms = 0;
                s_playback_start_ms = 0;
                strncpy(s_current_title, "Ready to Play", sizeof(s_current_title) - 1);
                s_current_artist[0] = '\0';

                ui_update_track(s_current_title, s_current_artist, SOURCE_NONE);
                ui_update_lyrics(NULL);
                ui_update_progress(0, 0);

                // If Bluetooth was stopped due to AirPlay preemption, re-initialize Bluetooth sink
                if (s_bt_stopped_for_airplay) {
                    log_i("AirPlay stream finished: re-initializing Bluetooth sink");
                    bt_sink_init(SPEAKER_NAME);
                    s_bt_stopped_for_airplay = false;
                }
            }

            s_system_audio_state = new_state;
        }

        xSemaphoreGive(s_state_mutex);
    }
}

// =============================================================================
// Lyrics Service Callback
// =============================================================================
static void on_lyrics_loaded(const LyricsData *lyrics, bool success) {
    if (success && lyrics && lyrics->loaded && lyrics->count > 0) {
        log_i("Lyrics loaded successfully: %d lines parsed for '%s'", (int)lyrics->count, lyrics->title);
        ui_update_lyrics(lyrics);
    } else {
        log_w("Lyrics not found or unavailable; display will show title & fallback view");
        ui_update_lyrics(NULL);
    }
}

// =============================================================================
// Bluetooth Callbacks
// =============================================================================
static void on_bt_track_changed(const TrackInfo *track) {
    if (!track) {
        return;
    }
    log_i("BT Track Notification: '%s' - '%s' [%s] (%u ms)",
          track->title, track->artist, track->album, (unsigned int)track->duration_ms);

    if (s_state_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_system_audio_state == STATE_BT_PLAYING || s_system_audio_state == STATE_IDLE) {
            strncpy(s_current_title, track->title, sizeof(s_current_title) - 1);
            s_current_title[sizeof(s_current_title) - 1] = '\0';
            strncpy(s_current_artist, track->artist, sizeof(s_current_artist) - 1);
            s_current_artist[sizeof(s_current_artist) - 1] = '\0';

            s_playback_start_ms = millis();
            s_track_duration_ms = track->duration_ms;

            ui_update_track(s_current_title, s_current_artist, SOURCE_BLUETOOTH);
            ui_update_progress(0, s_track_duration_ms);

            // Clear previous lyrics and trigger async fetch from LRCLIB on Core 1
            ui_update_lyrics(NULL);
            lyrics_fetch_async(s_current_title, s_current_artist, on_lyrics_loaded);
        }
        xSemaphoreGive(s_state_mutex);
    }
}

static void on_bt_status_changed(bool is_playing) {
    log_i("BT Playback Status changed: %s", is_playing ? "PLAYING" : "STOPPED/PAUSED");
    // Mutate state exclusively through arbitration to prevent decoupling or freezing active streams
    handle_system_audio_event(is_playing ? EVENT_BT_START : EVENT_BT_STOP);
}

// =============================================================================
// AirPlay Callbacks
// =============================================================================
static void on_airplay_track_changed(const char *title, const char *artist, const char *album) {
    (void)album;
    log_i("AirPlay Track Notification: '%s' - '%s'",
          title ? title : "", artist ? artist : "");

    if (s_state_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_system_audio_state == STATE_AIRPLAY_PLAYING || s_system_audio_state == STATE_IDLE) {
            if (title && *title) {
                strncpy(s_current_title, title, sizeof(s_current_title) - 1);
                s_current_title[sizeof(s_current_title) - 1] = '\0';
            }
            if (artist && *artist) {
                strncpy(s_current_artist, artist, sizeof(s_current_artist) - 1);
                s_current_artist[sizeof(s_current_artist) - 1] = '\0';
            }

            s_playback_start_ms = millis();
            s_track_duration_ms = 0; // AirPlay RTSP/DAAP stream progress interpolates locally

            ui_update_track(s_current_title, s_current_artist, SOURCE_AIRPLAY);
            ui_update_progress(0, 0);

            // Clear previous lyrics and trigger async fetch from LRCLIB on Core 1
            ui_update_lyrics(NULL);
            lyrics_fetch_async(s_current_title, s_current_artist, on_lyrics_loaded);
        }
        xSemaphoreGive(s_state_mutex);
    }
}

static void on_airplay_status_changed(bool is_active) {
    log_i("AirPlay Session Status changed: %s", is_active ? "ACTIVE" : "INACTIVE");
    // Mutate state exclusively through arbitration to prevent decoupling or freezing active streams
    handle_system_audio_event(is_active ? EVENT_AIRPLAY_START : EVENT_AIRPLAY_STOP);
}

// =============================================================================
// Arduino Setup & Main Loop
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    log_i("=== ESP32 AirPlay & Bluetooth Lyrics Speaker Booting ===");

    // 1. Verify PSRAM (Required for ESP32-WROVER-E audio buffers & LVGL display)
    if (psramInit()) {
        log_i("PSRAM Initialized: Total = %d bytes, Free = %d bytes",
              ESP.getPsramSize(), ESP.getFreePsram());
    } else {
        log_e("PSRAM Initialization Failed! Audio/UI stability may be degraded.");
    }

    // 2. Initialize Mutex for System State Machine
    s_state_mutex = xSemaphoreCreateMutex();

    // 3. Initialize I2S Audio Pipeline for MAX98357A (GPIO 25, 26, 27 on Core 0)
    if (!audio_i2s_init()) {
        log_e("Failed to initialize I2S audio driver!");
    } else {
        audio_i2s_set_volume(85); // 85% default comfortable speaker volume
        log_i("I2S Audio driver initialized (Volume: %d%%)", audio_i2s_get_volume());
    }

    // 4. Initialize LVGL Display UI (Layout A: 5-line scrolling lyrics, pinned to CORE_UI)
    if (!ui_init()) {
        log_e("Failed to initialize LVGL Display UI!");
    } else {
        log_i("LVGL Display UI initialized successfully on Core %d", CORE_UI);
    }

    // 5. Connect to Wi-Fi in background (Non-blocking)
    log_i("Initiating Wi-Fi connection to SSID '%s'...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // 6. Initialize Async Lyrics Service (Worker task pinned to CORE_UI)
    if (!lyrics_service_init()) {
        log_e("Failed to initialize Lyrics Service worker!");
    } else {
        log_i("Lyrics Service worker initialized on Core %d", CORE_UI);
    }

    // 7. Register Callbacks and Initialize Bluetooth A2DP Sink (Pinned to Core 0)
    bt_sink_register_track_callback(on_bt_track_changed);
    bt_sink_register_status_callback(on_bt_status_changed);
    if (!bt_sink_init(SPEAKER_NAME)) {
        log_e("Failed to initialize Bluetooth A2DP Sink!");
    } else {
        log_i("Bluetooth A2DP Sink started as '%s'", SPEAKER_NAME);
    }

    // 8. Register Callbacks and Initialize AirPlay RAOP Receiver (Pinned to Core 0)
    airplay_register_track_callback(on_airplay_track_changed);
    airplay_register_status_callback(on_airplay_status_changed);
    if (!airplay_receiver_init(SPEAKER_NAME)) {
        log_e("Failed to initialize AirPlay receiver!");
    } else {
        log_i("AirPlay receiver and mDNS service initialized as '%s'", SPEAKER_NAME);
    }

    log_i("=== System Initialization Complete: Ready for Playback ===");
}

void loop() {
    // Arduino loopTask runs on Core 1 (CORE_UI)
    // Periodically update Wi-Fi connection status indicator
    static bool s_last_wifi_connected = false;
    bool wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_connected != s_last_wifi_connected) {
        s_last_wifi_connected = wifi_connected;
        ui_set_wifi_status(wifi_connected);
        log_i("Wi-Fi Status changed: %s (IP: %s)",
              wifi_connected ? "CONNECTED" : "DISCONNECTED",
              wifi_connected ? WiFi.localIP().toString().c_str() : "0.0.0.0");
    }

    // Smooth playback progress interpolation and lyric synchronization
    if (s_is_playing && (s_system_audio_state == STATE_BT_PLAYING || s_system_audio_state == STATE_AIRPLAY_PLAYING)) {
        uint32_t current_ms = (uint32_t)(millis() - s_playback_start_ms);
        if (s_track_duration_ms > 0 && current_ms > s_track_duration_ms) {
            current_ms = s_track_duration_ms;
        }
        ui_update_progress(current_ms, s_track_duration_ms);
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz ticker for smooth UI progress & lyric sync
}
