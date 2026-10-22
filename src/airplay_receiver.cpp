#include "airplay_receiver.h"
#include "audio_i2s.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// Pure DAAP / DACP Metadata Parser (Native Host & Embedded Target)
// =============================================================================

static bool parse_daap_tag_recursive(const uint8_t *payload, size_t len, const char *tag_4cc, char *out_val, size_t out_val_len, int depth) {
    if (!payload || len < 8 || !tag_4cc || strlen(tag_4cc) != 4 || !out_val || out_val_len == 0 || depth > 16) {
        return false;
    }

    size_t offset = 0;
    while (offset + 8 <= len) {
        const uint8_t *tag_ptr = payload + offset;
        uint32_t tag_len = ((uint32_t)tag_ptr[4] << 24) |
                           ((uint32_t)tag_ptr[5] << 16) |
                           ((uint32_t)tag_ptr[6] << 8)  |
                           ((uint32_t)tag_ptr[7]);

        // Malformed tag length check: does tag_len exceed remaining payload?
        if ((size_t)tag_len > len - offset - 8) {
            return false;
        }

        const uint8_t *data_ptr = tag_ptr + 8;

        // Check if current 4CC matches requested tag
        if (memcmp(tag_ptr, tag_4cc, 4) == 0) {
            size_t to_copy = ((size_t)tag_len < out_val_len - 1) ? (size_t)tag_len : (out_val_len - 1);
            if (to_copy > 0) {
                memcpy(out_val, data_ptr, to_copy);
            }
            out_val[to_copy] = '\0';
            return true;
        }

        // If tag didn't match and could contain nested tags (DAAP container), recurse into payload
        if (tag_len >= 8) {
            if (parse_daap_tag_recursive(data_ptr, tag_len, tag_4cc, out_val, out_val_len, depth + 1)) {
                return true;
            }
        }

        offset += 8 + tag_len;
    }

    return false;
}

bool parse_daap_tag(const uint8_t *payload, size_t len, const char *tag_4cc, char *out_val, size_t out_val_len) {
    if (out_val && out_val_len > 0) {
        out_val[0] = '\0';
    }
    return parse_daap_tag_recursive(payload, len, tag_4cc, out_val, out_val_len, 0);
}

#if (defined(ESP_PLATFORM) || defined(ARDUINO)) && !defined(ARDUINO_ARCH_NATIVE)

// =============================================================================
// Embedded Implementation for ESP32-WROVER-E (RTSP Server, mDNS & Audio DMA)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "audio_i2s.h"

static AirplayTrackCallback s_track_callback = nullptr;
static AirplayStatusCallback s_status_callback = nullptr;
static volatile bool s_is_active = false;
static volatile bool s_server_running = false;

static TaskHandle_t s_rtsp_task_handle = NULL;
static TaskHandle_t s_rtp_task_handle = NULL;

static WiFiServer s_rtsp_server(5000);
static WiFiUDP s_rtp_udp;

static char s_service_name[64] = "ESP32-Lyrics-Speaker";

static void set_airplay_active(bool active) {
    if (s_is_active != active) {
        s_is_active = active;
        if (s_status_callback) {
            s_status_callback(active);
        }
    }
}

// RTP Audio receiver task running on CORE_AUDIO
static void rtp_audio_task(void *pvParameters) {
    (void)pvParameters;
    static uint8_t packet_buffer[2048];

    while (s_server_running) {
        int packet_size = s_rtp_udp.parsePacket();
        if (packet_size > 12) {
            int bytes_read = s_rtp_udp.read(packet_buffer, sizeof(packet_buffer));
            if (bytes_read > 12) {
                // RTP header is 12 bytes. Audio payload follows immediately.
                const uint8_t *audio_data = packet_buffer + 12;
                size_t audio_len = (size_t)(bytes_read - 12);
                audio_i2s_write(audio_data, audio_len);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    vTaskDelete(NULL);
}

// RTSP Server task handling AirPlay session negotiations
static void rtsp_server_task(void *pvParameters) {
    (void)pvParameters;

    while (s_server_running) {
        WiFiClient client = s_rtsp_server.available();
        if (client) {
            int cseq = 0;
            char method[32] = {0};
            int content_length = 0;
            char content_type[64] = {0};

            while (client.connected() && s_server_running) {
                if (client.available()) {
                    String line = client.readStringUntil('\n');
                    line.trim();

                    if (line.length() == 0) {
                        // Empty line signals end of RTSP headers
                        if (strlen(method) > 0) {
                            // Read body if content_length > 0
                            if (content_length > 0 && content_length < 8192) {
                                uint8_t *body_buf = (uint8_t *)malloc(content_length);
                                if (body_buf) {
                                    size_t read_bytes = client.readBytes(body_buf, content_length);
                                    if (read_bytes == (size_t)content_length &&
                                        strstr(content_type, "application/x-dmap-tagged") != NULL) {
                                        
                                        char title[AIRPLAY_METADATA_LEN] = {0};
                                        char artist[AIRPLAY_METADATA_LEN] = {0};
                                        char album[AIRPLAY_METADATA_LEN] = {0};

                                        bool has_title = parse_daap_tag(body_buf, content_length, "minm", title, sizeof(title));
                                        bool has_artist = parse_daap_tag(body_buf, content_length, "asar", artist, sizeof(artist));
                                        bool has_album = parse_daap_tag(body_buf, content_length, "asal", album, sizeof(album));

                                        if ((has_title || has_artist || has_album) && s_track_callback) {
                                            s_track_callback(title, artist, album);
                                        }
                                    }
                                    free(body_buf);
                                }
                            }

                            // Send RTSP response
                            if (strcmp(method, "OPTIONS") == 0) {
                                client.print("RTSP/1.0 200 OK\r\n");
                                client.printf("CSeq: %d\r\n", cseq);
                                client.print("Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, SET_PARAMETER, GET_PARAMETER\r\n");
                                client.print("Server: AirTunes/220.68\r\n\r\n");
                            } else if (strcmp(method, "ANNOUNCE") == 0) {
                                client.print("RTSP/1.0 200 OK\r\n");
                                client.printf("CSeq: %d\r\n\r\n", cseq);
                            } else if (strcmp(method, "SETUP") == 0) {
                                client.print("RTSP/1.0 200 OK\r\n");
                                client.printf("CSeq: %d\r\n", cseq);
                                client.print("Transport: RTP/AVP/UDP;unicast;mode=record;server_port=6000;control_port=6001;timing_port=6002\r\n");
                                client.print("Session: 1\r\n");
                                client.print("Audio-Jack-Status: connected\r\n\r\n");

                                // Ensure RTP receiver is listening
                                s_rtp_udp.begin(6000);
                                if (s_rtp_task_handle == NULL) {
                                    xTaskCreatePinnedToCore(rtp_audio_task, "airplay_rtp", 4096, NULL, 5, &s_rtp_task_handle, CORE_AUDIO);
                                }
                            } else if (strcmp(method, "RECORD") == 0) {
                                set_airplay_active(true);
                                client.print("RTSP/1.0 200 OK\r\n");
                                client.printf("CSeq: %d\r\n", cseq);
                                client.print("Audio-Latency: 2205\r\n\r\n");
                            } else if (strcmp(method, "SET_PARAMETER") == 0) {
                                client.print("RTSP/1.0 200 OK\r\n");
                                client.printf("CSeq: %d\r\n\r\n", cseq);
                            } else if (strcmp(method, "FLUSH") == 0) {
                                client.print("RTSP/1.0 200 OK\r\n");
                                client.printf("CSeq: %d\r\n\r\n", cseq);
                            } else if (strcmp(method, "TEARDOWN") == 0) {
                                set_airplay_active(false);
                                client.print("RTSP/1.0 200 OK\r\n");
                                client.printf("CSeq: %d\r\n", cseq);
                                client.print("Connection: close\r\n\r\n");
                                break;
                            } else {
                                client.print("RTSP/1.0 200 OK\r\n");
                                client.printf("CSeq: %d\r\n\r\n", cseq);
                            }

                            // Reset per-request state
                            method[0] = '\0';
                            cseq = 0;
                            content_length = 0;
                            content_type[0] = '\0';
                        }
                    } else {
                        // Parse RTSP request line
                        if (line.startsWith("OPTIONS ") || line.startsWith("ANNOUNCE ") ||
                            line.startsWith("SETUP ")   || line.startsWith("RECORD ")   ||
                            line.startsWith("PAUSE ")   || line.startsWith("FLUSH ")    ||
                            line.startsWith("TEARDOWN ")|| line.startsWith("SET_PARAMETER ") ||
                            line.startsWith("GET_PARAMETER ")) {
                            
                            int space_idx = line.indexOf(' ');
                            if (space_idx > 0 && space_idx < (int)sizeof(method)) {
                                strncpy(method, line.substring(0, space_idx).c_str(), sizeof(method) - 1);
                                method[sizeof(method) - 1] = '\0';
                            }
                        } else if (line.startsWith("CSeq:")) {
                            cseq = line.substring(5).toInt();
                        } else if (line.startsWith("Content-Length:")) {
                            content_length = line.substring(15).toInt();
                        } else if (line.startsWith("Content-Type:")) {
                            strncpy(content_type, line.substring(13).c_str(), sizeof(content_type) - 1);
                            content_type[sizeof(content_type) - 1] = '\0';
                        }
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
            }
            client.stop();
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    vTaskDelete(NULL);
}

bool airplay_receiver_init(const char *service_name) {
    if (service_name && strlen(service_name) > 0) {
        strncpy(s_service_name, service_name, sizeof(s_service_name) - 1);
        s_service_name[sizeof(s_service_name) - 1] = '\0';
    }

    // Publish mDNS RAOP and AirPlay services
    if (!MDNS.begin(s_service_name)) {
        log_w("MDNS responder failed to initialize, continuing...");
    } else {
        MDNS.addService("raop", "tcp", 5000);
        MDNS.addServiceTxt("raop", "tcp", "cn", "0,1");
        MDNS.addServiceTxt("raop", "tcp", "ch", "2");
        MDNS.addServiceTxt("raop", "tcp", "ss", "16");
        MDNS.addServiceTxt("raop", "tcp", "sr", "44100");
        MDNS.addServiceTxt("raop", "tcp", "vn", "3");
        MDNS.addServiceTxt("raop", "tcp", "tp", "UDP");
        MDNS.addServiceTxt("raop", "tcp", "md", "0,1,2");
        MDNS.addServiceTxt("raop", "tcp", "pw", "false");

        MDNS.addService("airplay", "tcp", 7000);
        MDNS.addServiceTxt("airplay", "tcp", "model", "ESP32-Speaker");
    }

    s_server_running = true;
    s_rtsp_server.begin(5000);

    if (s_rtsp_task_handle == NULL) {
        xTaskCreatePinnedToCore(rtsp_server_task, "airplay_rtsp", 8192, NULL, 5, &s_rtsp_task_handle, CORE_AUDIO);
    }

    return true;
}

void airplay_receiver_stop(void) {
    s_server_running = false;
    set_airplay_active(false);

    s_rtsp_server.end();
    s_rtp_udp.stop();

    if (s_rtsp_task_handle != NULL) {
        vTaskDelete(s_rtsp_task_handle);
        s_rtsp_task_handle = NULL;
    }
    if (s_rtp_task_handle != NULL) {
        vTaskDelete(s_rtp_task_handle);
        s_rtp_task_handle = NULL;
    }
}

bool airplay_is_active(void) {
    return s_is_active;
}

void airplay_register_track_callback(AirplayTrackCallback cb) {
    s_track_callback = cb;
}

void airplay_register_status_callback(AirplayStatusCallback cb) {
    s_status_callback = cb;
}

#else

// =============================================================================
// Native / Host Mock Implementation for Unit Tests
// =============================================================================

static bool s_mock_initialized = false;
static bool s_mock_active = false;
static AirplayTrackCallback s_mock_track_cb = nullptr;
static AirplayStatusCallback s_mock_status_cb = nullptr;

bool airplay_receiver_init(const char *service_name) {
    (void)service_name;
    s_mock_initialized = true;
    return true;
}

void airplay_receiver_stop(void) {
    s_mock_initialized = false;
    if (s_mock_active) {
        s_mock_active = false;
        if (s_mock_status_cb) {
            s_mock_status_cb(false);
        }
    }
}

bool airplay_is_active(void) {
    return s_mock_active;
}

void airplay_register_track_callback(AirplayTrackCallback cb) {
    s_mock_track_cb = cb;
}

void airplay_register_status_callback(AirplayStatusCallback cb) {
    s_mock_status_cb = cb;
}

void airplay_mock_set_active(bool active) {
    if (s_mock_active != active) {
        s_mock_active = active;
        if (s_mock_status_cb) {
            s_mock_status_cb(active);
        }
    }
}

void airplay_mock_trigger_track(const char *title, const char *artist, const char *album) {
    if (s_mock_track_cb) {
        s_mock_track_cb(title ? title : "", artist ? artist : "", album ? album : "");
    }
}

void airplay_mock_feed_daap_metadata(const uint8_t *payload, size_t len) {
    char title[AIRPLAY_METADATA_LEN] = {0};
    char artist[AIRPLAY_METADATA_LEN] = {0};
    char album[AIRPLAY_METADATA_LEN] = {0};

    bool has_title = parse_daap_tag(payload, len, "minm", title, sizeof(title));
    bool has_artist = parse_daap_tag(payload, len, "asar", artist, sizeof(artist));
    bool has_album = parse_daap_tag(payload, len, "asal", album, sizeof(album));

    if ((has_title || has_artist || has_album) && s_mock_track_cb) {
        s_mock_track_cb(title, artist, album);
    }
}

void airplay_mock_feed_audio(const uint8_t *pcm_data, size_t len) {
    if (pcm_data && len > 0) {
        audio_i2s_write(pcm_data, len);
    }
}

void airplay_mock_reset(void) {
    s_mock_initialized = false;
    s_mock_active = false;
    s_mock_track_cb = nullptr;
    s_mock_status_cb = nullptr;
}

#endif
