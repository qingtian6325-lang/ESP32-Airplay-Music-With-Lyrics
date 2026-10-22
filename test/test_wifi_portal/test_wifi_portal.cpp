#include <unity.h>
#include <string.h>
#include <stdbool.h>
#include "wifi_portal.h"

static WifiPortalState s_cb_state = WIFI_PORTAL_STATE_IDLE;
static char s_cb_msg[64] = {0};
static int s_cb_count = 0;

static void on_portal_state_changed(WifiPortalState state, const char *msg) {
    s_cb_state = state;
    if (msg) {
        strncpy(s_cb_msg, msg, sizeof(s_cb_msg) - 1);
        s_cb_msg[sizeof(s_cb_msg) - 1] = '\0';
    } else {
        s_cb_msg[0] = '\0';
    }
    s_cb_count++;
}

void setUp(void) {
    s_cb_state = WIFI_PORTAL_STATE_IDLE;
    s_cb_msg[0] = '\0';
    s_cb_count = 0;
#if !(defined(ESP_PLATFORM) || defined(ARDUINO)) || defined(ARDUINO_ARCH_NATIVE)
    wifi_portal_mock_reset();
#endif
}

void tearDown(void) {
}

void test_wifi_portal_init(void) {
    TEST_ASSERT_TRUE(wifi_portal_init(on_portal_state_changed));
    TEST_ASSERT_EQUAL_INT(WIFI_PORTAL_STATE_IDLE, wifi_portal_get_state());
    TEST_ASSERT_EQUAL_INT(1, s_cb_count);
}

void test_wifi_portal_no_credentials(void) {
    wifi_portal_init(on_portal_state_changed);
    TEST_ASSERT_FALSE(wifi_portal_has_saved_credentials());

    char ssid[32] = {0};
    TEST_ASSERT_FALSE(wifi_portal_get_saved_ssid(ssid, sizeof(ssid)));
    TEST_ASSERT_EQUAL_STRING("", ssid);
}

void test_wifi_portal_save_and_retrieve_credentials(void) {
    wifi_portal_init(on_portal_state_changed);

    TEST_ASSERT_TRUE(wifi_portal_save_credentials("Home_WiFi", "secret123"));
    TEST_ASSERT_TRUE(wifi_portal_has_saved_credentials());

    char ssid[32] = {0};
    TEST_ASSERT_TRUE(wifi_portal_get_saved_ssid(ssid, sizeof(ssid)));
    TEST_ASSERT_EQUAL_STRING("Home_WiFi", ssid);
}

void test_wifi_portal_clear_credentials(void) {
    wifi_portal_init(on_portal_state_changed);

    wifi_portal_save_credentials("TestSSID", "pass");
    TEST_ASSERT_TRUE(wifi_portal_has_saved_credentials());

    wifi_portal_clear_credentials();
    TEST_ASSERT_FALSE(wifi_portal_has_saved_credentials());
}

void test_wifi_portal_connect_saved_success(void) {
    wifi_portal_init(on_portal_state_changed);
    wifi_portal_save_credentials("LivingRoom_2.4G", "mypassword");

#if !(defined(ESP_PLATFORM) || defined(ARDUINO)) || defined(ARDUINO_ARCH_NATIVE)
    wifi_portal_mock_set_connect_result(true);
#endif

    TEST_ASSERT_TRUE(wifi_portal_connect_saved(5000));
    TEST_ASSERT_EQUAL_INT(WIFI_PORTAL_STATE_CONNECTED, wifi_portal_get_state());

    char ip[32] = {0};
    wifi_portal_get_ip_string(ip, sizeof(ip));
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", ip);
}

void test_wifi_portal_connect_saved_fail(void) {
    wifi_portal_init(on_portal_state_changed);
    wifi_portal_save_credentials("Nonexistent_WiFi", "wrongpass");

#if !(defined(ESP_PLATFORM) || defined(ARDUINO)) || defined(ARDUINO_ARCH_NATIVE)
    wifi_portal_mock_set_connect_result(false);
#endif

    TEST_ASSERT_FALSE(wifi_portal_connect_saved(5000));
    TEST_ASSERT_EQUAL_INT(WIFI_PORTAL_STATE_CONNECT_FAILED, wifi_portal_get_state());
}

void test_wifi_portal_start_and_stop_ap(void) {
    wifi_portal_init(on_portal_state_changed);

    TEST_ASSERT_TRUE(wifi_portal_start_ap());
    TEST_ASSERT_EQUAL_INT(WIFI_PORTAL_STATE_AP_CONFIGURING, wifi_portal_get_state());

    char ip[32] = {0};
    wifi_portal_get_ip_string(ip, sizeof(ip));
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", ip);

    wifi_portal_stop_ap();
}

#if defined(ARDUINO)
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_wifi_portal_init);
    RUN_TEST(test_wifi_portal_no_credentials);
    RUN_TEST(test_wifi_portal_save_and_retrieve_credentials);
    RUN_TEST(test_wifi_portal_clear_credentials);
    RUN_TEST(test_wifi_portal_connect_saved_success);
    RUN_TEST(test_wifi_portal_connect_saved_fail);
    RUN_TEST(test_wifi_portal_start_and_stop_ap);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_wifi_portal_init);
    RUN_TEST(test_wifi_portal_no_credentials);
    RUN_TEST(test_wifi_portal_save_and_retrieve_credentials);
    RUN_TEST(test_wifi_portal_clear_credentials);
    RUN_TEST(test_wifi_portal_connect_saved_success);
    RUN_TEST(test_wifi_portal_connect_saved_fail);
    RUN_TEST(test_wifi_portal_start_and_stop_ap);
    return UNITY_END();
}
#endif
