#include "wifi_portal.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

static Preferences s_prefs;
static WebServer s_server(80);
static DNSServer s_dns_server;
static WifiPortalState s_state = WIFI_PORTAL_STATE_IDLE;
static WifiPortalStateCallback s_state_cb = NULL;
static char s_current_ip[32] = "0.0.0.0";
static bool s_ap_running = false;
static bool s_new_credentials_received = false;
static char s_pending_ssid[WIFI_PORTAL_MAX_SSID_LEN] = {0};
static char s_pending_pass[WIFI_PORTAL_MAX_PASS_LEN] = {0};

static void set_state(WifiPortalState state, const char *msg) {
    s_state = state;
    if (s_state_cb) {
        s_state_cb(state, msg);
    }
}

static const char PORTAL_HTML_HEADER[] PROGMEM = 
"<!DOCTYPE html><html lang='zh-CN'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<title>ESP32 歌词音箱 Wi-Fi 配置</title>"
"<style>"
"body{background:#0f172a;color:#f8fafc;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;margin:0;padding:20px;display:flex;justify-content:center;}"
".card{background:#1e293b;border:1px solid #334155;border-radius:16px;max-width:380px;width:100%;padding:24px;box-shadow:0 10px 25px rgba(0,0,0,0.5);}"
"h2{margin:0 0 4px;font-size:20px;color:#38bdf8;text-align:center;}"
".subtitle{text-align:center;font-size:12px;color:#94a3b8;margin-bottom:20px;}"
".wifi-list{max-height:160px;overflow-y:auto;border:1px solid #334155;border-radius:10px;margin-bottom:16px;background:#0f172a;}"
".wifi-item{padding:10px 14px;border-bottom:1px solid #1e293b;cursor:pointer;display:flex;justify-content:space-between;align-items:center;font-size:13px;}"
".wifi-item:hover{background:#1e293b;color:#38bdf8;}"
".wifi-item:last-child{border-bottom:none;}"
"label{display:block;font-size:12px;color:#cbd5e1;margin-bottom:6px;}"
"input{width:100%;box-sizing:border-box;padding:10px 12px;background:#0f172a;border:1px solid #475569;border-radius:8px;color:#fff;font-size:14px;margin-bottom:14px;outline:none;}"
"input:focus{border-color:#38bdf8;}"
"button{width:100%;padding:12px;background:linear-gradient(135deg,#0284c7,#2563eb);color:#fff;border:none;border-radius:10px;font-size:14px;font-weight:600;cursor:pointer;transition:0.2s;}"
"button:active{transform:scale(0.98);}"
".tip{font-size:11px;color:#64748b;margin-top:14px;text-align:center;line-height:1.5;}"
"</style></head><body><div class='card'>"
"<h2>🎵 ESP32 歌词音箱</h2>"
"<div class='subtitle'>请在下方选择 2.4G Wi-Fi 完成配网</div>"
"<div class='wifi-list'>";

static const char PORTAL_HTML_FOOTER[] PROGMEM = 
"</div>"
"<form action='/save' method='POST'>"
"<label>Wi-Fi 名称 (SSID)</label>"
"<input id='ssid' name='ssid' required placeholder='例如: MyHome_2.4G'>"
"<label>Wi-Fi 密码</label>"
"<input id='pass' name='pass' type='password' placeholder='请输入密码 (开放网络留空)'>"
"<button type='submit'>💾 保存并连接音箱</button>"
"</form>"
"<div class='tip'>💡 密码将永久加密保存在 ESP32 内部 Flash 中，下次开机自动秒连。</div>"
"</div>"
"<script>"
"function sel(s){document.getElementById('ssid').value=s;document.getElementById('pass').focus();}"
"</script></body></html>";

static void handle_portal_root(void) {
    String html = FPSTR(PORTAL_HTML_HEADER);

    int n = WiFi.scanNetworks();
    if (n == 0) {
        html += "<div class='wifi-item' style='color:#94a3b8;'>未扫描到附近的 Wi-Fi</div>";
    } else {
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);
            int quality = 2 * (rssi + 100);
            if (quality > 100) quality = 100;
            if (quality < 0) quality = 0;

            html += "<div class='wifi-item' onclick=\"sel('" + ssid + "')\">";
            html += "<span>📶 " + ssid + "</span>";
            html += "<span style='font-size:11px;color:#38bdf8;'>" + String(quality) + "%</span>";
            html += "</div>";
        }
    }

    html += FPSTR(PORTAL_HTML_FOOTER);
    s_server.send(200, "text/html", html);
}

static void handle_portal_save(void) {
    if (s_server.hasArg("ssid")) {
        String ssid = s_server.arg("ssid");
        String pass = s_server.hasArg("pass") ? s_server.arg("pass") : "";

        strncpy(s_pending_ssid, ssid.c_str(), sizeof(s_pending_ssid) - 1);
        strncpy(s_pending_pass, pass.c_str(), sizeof(s_pending_pass) - 1);
        s_new_credentials_received = true;

        wifi_portal_save_credentials(s_pending_ssid, s_pending_pass);

        String resp = "<!DOCTYPE html><html lang='zh-CN'><head><meta charset='UTF-8'>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
                      "<title>保存成功</title>"
                      "<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;text-align:center;padding:40px;}"
                      ".box{background:#1e293b;border-radius:16px;padding:30px;max-width:320px;margin:0 auto;border:1px solid #334155;}"
                      "h3{color:#38bdf8;}</style></head><body>"
                      "<div class='box'><h3>✅ 配置已保存！</h3>"
                      "<p style='font-size:13px;color:#94a3b8;'>音箱正在尝试连接【" + ssid + "】...</p>"
                      "<p style='font-size:12px;color:#64748b;'>您可以关闭此网页并断开热点了。</p>"
                      "</div></body></html>";
        s_server.send(200, "text/html", resp);
        set_state(WIFI_PORTAL_STATE_CONNECTING_NEW, s_pending_ssid);
    } else {
        s_server.send(400, "text/plain", "Bad Request: SSID missing");
    }
}

static void handle_captive_redirect(void) {
    s_server.sendHeader("Location", "http://192.168.4.1/", true);
    s_server.send(302, "text/plain", "");
}

bool wifi_portal_init(WifiPortalStateCallback cb) {
    s_state_cb = cb;
    s_prefs.begin("wifi_cfg", false);
    set_state(WIFI_PORTAL_STATE_IDLE, "Initialized");
    return true;
}

bool wifi_portal_has_saved_credentials(void) {
    String ssid = s_prefs.getString("ssid", "");
    if (ssid.length() > 0) {
        return true;
    }
    // Check fallback config.h credentials if not placeholder
    if (strcmp(WIFI_SSID, "Your_WiFi_SSID") != 0 && strlen(WIFI_SSID) > 0) {
        return true;
    }
    return false;
}

bool wifi_portal_get_saved_ssid(char *out_ssid, size_t max_len) {
    if (!out_ssid || max_len == 0) return false;
    out_ssid[0] = '\0';

    String ssid = s_prefs.getString("ssid", "");
    if (ssid.length() > 0) {
        strncpy(out_ssid, ssid.c_str(), max_len - 1);
        out_ssid[max_len - 1] = '\0';
        return true;
    }

    if (strcmp(WIFI_SSID, "Your_WiFi_SSID") != 0 && strlen(WIFI_SSID) > 0) {
        strncpy(out_ssid, WIFI_SSID, max_len - 1);
        out_ssid[max_len - 1] = '\0';
        return true;
    }
    return false;
}

bool wifi_portal_save_credentials(const char *ssid, const char *password) {
    if (!ssid || ssid[0] == '\0') return false;
    s_prefs.putString("ssid", ssid);
    s_prefs.putString("pass", password ? password : "");
    return true;
}

void wifi_portal_clear_credentials(void) {
    s_prefs.remove("ssid");
    s_prefs.remove("pass");
}

bool wifi_portal_connect_saved(uint32_t timeout_ms) {
    char ssid[WIFI_PORTAL_MAX_SSID_LEN] = {0};
    char pass[WIFI_PORTAL_MAX_PASS_LEN] = {0};

    String nvs_ssid = s_prefs.getString("ssid", "");
    if (nvs_ssid.length() > 0) {
        strncpy(ssid, nvs_ssid.c_str(), sizeof(ssid) - 1);
        String nvs_pass = s_prefs.getString("pass", "");
        strncpy(pass, nvs_pass.c_str(), sizeof(pass) - 1);
    } else if (strcmp(WIFI_SSID, "Your_WiFi_SSID") != 0 && strlen(WIFI_SSID) > 0) {
        strncpy(ssid, WIFI_SSID, sizeof(ssid) - 1);
        strncpy(pass, WIFI_PASSWORD, sizeof(pass) - 1);
    } else {
        return false;
    }

    set_state(WIFI_PORTAL_STATE_CONNECTING_SAVED, ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass[0] != '\0' ? pass : NULL);

    uint32_t start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start_ms) < timeout_ms) {
        delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        snprintf(s_current_ip, sizeof(s_current_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        set_state(WIFI_PORTAL_STATE_CONNECTED, s_current_ip);
        return true;
    }

    set_state(WIFI_PORTAL_STATE_CONNECT_FAILED, "Connect timeout");
    return false;
}

bool wifi_portal_start_ap(void) {
    if (s_ap_running) return true;

    WiFi.mode(WIFI_AP_STA);
    bool ap_ok = WiFi.softAP(WIFI_PORTAL_AP_SSID);
    if (!ap_ok) {
        return false;
    }

    IPAddress ap_ip = WiFi.softAPIP();
    snprintf(s_current_ip, sizeof(s_current_ip), "%d.%d.%d.%d", ap_ip[0], ap_ip[1], ap_ip[2], ap_ip[3]);

    // DNS server redirects all domains to 192.168.4.1 for Captive Portal auto-popup
    s_dns_server.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns_server.start(53, "*", ap_ip);

    // Captive Portal HTTP routes
    s_server.on("/", HTTP_GET, handle_portal_root);
    s_server.on("/save", HTTP_POST, handle_portal_save);

    // Standard OS captive portal check routes (iOS, Android, Windows)
    s_server.on("/hotspot-detect.html", HTTP_GET, handle_portal_root);
    s_server.on("/generate_204", HTTP_GET, handle_portal_root);
    s_server.on("/gen_204", HTTP_GET, handle_portal_root);
    s_server.on("/ncsi.txt", HTTP_GET, handle_portal_root);
    s_server.onNotFound(handle_captive_redirect);

    s_server.begin();
    s_ap_running = true;
    s_new_credentials_received = false;

    set_state(WIFI_PORTAL_STATE_AP_CONFIGURING, WIFI_PORTAL_AP_SSID);
    return true;
}

void wifi_portal_process_ap(void) {
    if (!s_ap_running) return;

    s_dns_server.processNextRequest();
    s_server.handleClient();

    if (s_new_credentials_received) {
        s_new_credentials_received = false;
        delay(1000); // Give client time to receive response page
        wifi_portal_stop_ap();
        wifi_portal_connect_saved(15000);
    }
}

void wifi_portal_stop_ap(void) {
    if (!s_ap_running) return;

    s_server.stop();
    s_dns_server.stop();
    WiFi.softAPdisconnect(true);
    s_ap_running = false;
}

WifiPortalState wifi_portal_get_state(void) {
    return s_state;
}

void wifi_portal_get_ip_string(char *out_ip, size_t max_len) {
    if (!out_ip || max_len == 0) return;
    strncpy(out_ip, s_current_ip, max_len - 1);
    out_ip[max_len - 1] = '\0';
}

#else

// =============================================================================
// Native / Host Mock Implementation for Unit Tests
// =============================================================================

static WifiPortalState s_mock_state = WIFI_PORTAL_STATE_IDLE;
static WifiPortalStateCallback s_mock_cb = NULL;
static char s_mock_saved_ssid[WIFI_PORTAL_MAX_SSID_LEN] = {0};
static char s_mock_saved_pass[WIFI_PORTAL_MAX_PASS_LEN] = {0};
static char s_mock_ip[32] = "0.0.0.0";
static bool s_mock_connect_result = true;
static bool s_mock_ap_running = false;

static void mock_set_state(WifiPortalState state, const char *msg) {
    s_mock_state = state;
    if (s_mock_cb) {
        s_mock_cb(state, msg);
    }
}

bool wifi_portal_init(WifiPortalStateCallback cb) {
    s_mock_cb = cb;
    mock_set_state(WIFI_PORTAL_STATE_IDLE, "Initialized");
    return true;
}

bool wifi_portal_has_saved_credentials(void) {
    return s_mock_saved_ssid[0] != '\0';
}

bool wifi_portal_get_saved_ssid(char *out_ssid, size_t max_len) {
    if (!out_ssid || max_len == 0) return false;
    if (s_mock_saved_ssid[0] == '\0') {
        out_ssid[0] = '\0';
        return false;
    }
    strncpy(out_ssid, s_mock_saved_ssid, max_len - 1);
    out_ssid[max_len - 1] = '\0';
    return true;
}

bool wifi_portal_save_credentials(const char *ssid, const char *password) {
    if (!ssid || ssid[0] == '\0') return false;
    strncpy(s_mock_saved_ssid, ssid, sizeof(s_mock_saved_ssid) - 1);
    s_mock_saved_ssid[sizeof(s_mock_saved_ssid) - 1] = '\0';
    if (password) {
        strncpy(s_mock_saved_pass, password, sizeof(s_mock_saved_pass) - 1);
        s_mock_saved_pass[sizeof(s_mock_saved_pass) - 1] = '\0';
    } else {
        s_mock_saved_pass[0] = '\0';
    }
    return true;
}

void wifi_portal_clear_credentials(void) {
    s_mock_saved_ssid[0] = '\0';
    s_mock_saved_pass[0] = '\0';
}

bool wifi_portal_connect_saved(uint32_t timeout_ms) {
    (void)timeout_ms;
    if (s_mock_saved_ssid[0] == '\0') {
        return false;
    }
    mock_set_state(WIFI_PORTAL_STATE_CONNECTING_SAVED, s_mock_saved_ssid);
    if (s_mock_connect_result) {
        strncpy(s_mock_ip, "192.168.1.100", sizeof(s_mock_ip) - 1);
        mock_set_state(WIFI_PORTAL_STATE_CONNECTED, s_mock_ip);
        return true;
    }
    mock_set_state(WIFI_PORTAL_STATE_CONNECT_FAILED, "Timeout");
    return false;
}

bool wifi_portal_start_ap(void) {
    s_mock_ap_running = true;
    strncpy(s_mock_ip, "192.168.4.1", sizeof(s_mock_ip) - 1);
    mock_set_state(WIFI_PORTAL_STATE_AP_CONFIGURING, WIFI_PORTAL_AP_SSID);
    return true;
}

void wifi_portal_process_ap(void) {
    // No-op on host mock
}

void wifi_portal_stop_ap(void) {
    s_mock_ap_running = false;
}

WifiPortalState wifi_portal_get_state(void) {
    return s_mock_state;
}

void wifi_portal_get_ip_string(char *out_ip, size_t max_len) {
    if (!out_ip || max_len == 0) return;
    strncpy(out_ip, s_mock_ip, max_len - 1);
    out_ip[max_len - 1] = '\0';
}

void wifi_portal_mock_reset(void) {
    s_mock_state = WIFI_PORTAL_STATE_IDLE;
    s_mock_saved_ssid[0] = '\0';
    s_mock_saved_pass[0] = '\0';
    strcpy(s_mock_ip, "0.0.0.0");
    s_mock_connect_result = true;
    s_mock_ap_running = false;
}

void wifi_portal_mock_set_saved_credentials(const char *ssid, const char *pass) {
    wifi_portal_save_credentials(ssid, pass);
}

void wifi_portal_mock_set_connect_result(bool success) {
    s_mock_connect_result = success;
}

#endif
