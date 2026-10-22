#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_PORTAL_AP_SSID        "ESP32-Lyrics-Setup"
#define WIFI_PORTAL_MAX_SSID_LEN   32
#define WIFI_PORTAL_MAX_PASS_LEN   64

typedef enum {
    WIFI_PORTAL_STATE_IDLE = 0,
    WIFI_PORTAL_STATE_CONNECTING_SAVED,
    WIFI_PORTAL_STATE_AP_CONFIGURING,
    WIFI_PORTAL_STATE_CONNECTING_NEW,
    WIFI_PORTAL_STATE_CONNECTED,
    WIFI_PORTAL_STATE_CONNECT_FAILED
} WifiPortalState;

typedef void (*WifiPortalStateCallback)(WifiPortalState state, const char *message);

/**
 * @brief Initialize the Wi-Fi portal module and read credentials from NVS.
 * @param cb Optional state change notification callback.
 * @return true if initialized successfully.
 */
bool wifi_portal_init(WifiPortalStateCallback cb);

/**
 * @brief Check if valid Wi-Fi credentials have been saved to NVS storage.
 * @return true if SSID is non-empty.
 */
bool wifi_portal_has_saved_credentials(void);

/**
 * @brief Get the currently stored SSID.
 * @param out_ssid Destination buffer.
 * @param max_len Size of destination buffer.
 * @return true if SSID exists.
 */
bool wifi_portal_get_saved_ssid(char *out_ssid, size_t max_len);

/**
 * @brief Save new Wi-Fi credentials to persistent NVS storage.
 * @param ssid Wi-Fi SSID.
 * @param password Wi-Fi password (empty string if open network).
 * @return true if saved successfully.
 */
bool wifi_portal_save_credentials(const char *ssid, const char *password);

/**
 * @brief Clear saved Wi-Fi credentials from NVS storage.
 */
void wifi_portal_clear_credentials(void);

/**
 * @brief Attempt to connect to the saved Wi-Fi network.
 * @param timeout_ms Connection timeout in milliseconds (e.g. 10000ms).
 * @return true if connected successfully with a valid IP.
 */
bool wifi_portal_connect_saved(uint32_t timeout_ms);

/**
 * @brief Start AP setup mode with captive portal web server & DNS hijack.
 * Creates "ESP32-Lyrics-Setup" open Wi-Fi network at 192.168.4.1.
 * @return true if AP started successfully.
 */
bool wifi_portal_start_ap(void);

/**
 * @brief Process pending DNS and HTTP client requests in AP mode.
 * Should be called periodically in the main or Core 0 loop when in AP mode.
 */
void wifi_portal_process_ap(void);

/**
 * @brief Stop AP mode and release captive portal server resources.
 */
void wifi_portal_stop_ap(void);

/**
 * @brief Get the current Wi-Fi portal state.
 */
WifiPortalState wifi_portal_get_state(void);

/**
 * @brief Get the assigned local IP address string (e.g. "192.168.1.105" or "192.168.4.1").
 * @param out_ip Destination buffer.
 * @param max_len Size of destination buffer.
 */
void wifi_portal_get_ip_string(char *out_ip, size_t max_len);

#if !(defined(ESP_PLATFORM) || defined(ARDUINO)) || defined(ARDUINO_ARCH_NATIVE)
/**
 * @brief Host mock helpers for testing without ESP32 hardware or real Wi-Fi.
 */
void wifi_portal_mock_reset(void);
void wifi_portal_mock_set_saved_credentials(const char *ssid, const char *pass);
void wifi_portal_mock_set_connect_result(bool success);
#endif

#ifdef __cplusplus
}
#endif

#endif // WIFI_PORTAL_H
