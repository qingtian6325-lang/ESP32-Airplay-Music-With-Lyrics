#include <unity.h>
#include <string.h>
#include <stdint.h>
#include "airplay_receiver.h"
#include "audio_i2s.h"

static char s_last_title[AIRPLAY_METADATA_LEN] = {0};
static char s_last_artist[AIRPLAY_METADATA_LEN] = {0};
static char s_last_album[AIRPLAY_METADATA_LEN] = {0};
static int s_track_cb_count = 0;

static bool s_last_active = false;
static int s_status_cb_count = 0;

static void on_track_change_test(const char *title, const char *artist, const char *album) {
    if (title) {
        strncpy(s_last_title, title, sizeof(s_last_title) - 1);
        s_last_title[sizeof(s_last_title) - 1] = '\0';
    } else {
        s_last_title[0] = '\0';
    }
    if (artist) {
        strncpy(s_last_artist, artist, sizeof(s_last_artist) - 1);
        s_last_artist[sizeof(s_last_artist) - 1] = '\0';
    } else {
        s_last_artist[0] = '\0';
    }
    if (album) {
        strncpy(s_last_album, album, sizeof(s_last_album) - 1);
        s_last_album[sizeof(s_last_album) - 1] = '\0';
    } else {
        s_last_album[0] = '\0';
    }
    s_track_cb_count++;
}

static void on_status_change_test(bool is_active) {
    s_last_active = is_active;
    s_status_cb_count++;
}

void setUp(void) {
    memset(s_last_title, 0, sizeof(s_last_title));
    memset(s_last_artist, 0, sizeof(s_last_artist));
    memset(s_last_album, 0, sizeof(s_last_album));
    s_track_cb_count = 0;
    s_last_active = false;
    s_status_cb_count = 0;
    airplay_mock_reset();
}

void tearDown(void) {
    airplay_receiver_stop();
}

void test_daap_minm_parsing(void) {
    const uint8_t raw_daap[] = { 'm','i','n','m', 0, 0, 0, 4, 'S','o','n','g' };
    char title[32] = {0};
    bool res = parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, sizeof(title));
    TEST_ASSERT_TRUE(res);
    TEST_ASSERT_EQUAL_STRING("Song", title);
}

void test_daap_asar_parsing(void) {
    const uint8_t raw_daap[] = { 'a','s','a','r', 0, 0, 0, 10, 'E','d',' ','S','h','e','e','r','a','n' };
    char artist[32] = {0};
    bool res = parse_daap_tag(raw_daap, sizeof(raw_daap), "asar", artist, sizeof(artist));
    TEST_ASSERT_TRUE(res);
    TEST_ASSERT_EQUAL_STRING("Ed Sheeran", artist);
}

void test_daap_asal_parsing(void) {
    const uint8_t raw_daap[] = { 'a','s','a','l', 0, 0, 0, 6, 'D','i','v','i','d','e' };
    char album[32] = {0};
    bool res = parse_daap_tag(raw_daap, sizeof(raw_daap), "asal", album, sizeof(album));
    TEST_ASSERT_TRUE(res);
    TEST_ASSERT_EQUAL_STRING("Divide", album);
}

void test_daap_multiple_sibling_tags(void) {
    const uint8_t raw_daap[] = {
        'm','i','n','m', 0, 0, 0, 4, 'S','o','n','g',
        'a','s','a','r', 0, 0, 0, 6, 'A','r','t','i','s','t',
        'a','s','a','l', 0, 0, 0, 5, 'A','l','b','u','m'
    };

    char title[32] = {0};
    char artist[32] = {0};
    char album[32] = {0};

    TEST_ASSERT_TRUE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, sizeof(title)));
    TEST_ASSERT_EQUAL_STRING("Song", title);

    TEST_ASSERT_TRUE(parse_daap_tag(raw_daap, sizeof(raw_daap), "asar", artist, sizeof(artist)));
    TEST_ASSERT_EQUAL_STRING("Artist", artist);

    TEST_ASSERT_TRUE(parse_daap_tag(raw_daap, sizeof(raw_daap), "asal", album, sizeof(album)));
    TEST_ASSERT_EQUAL_STRING("Album", album);
}

void test_daap_nested_tags_in_container(void) {
    // Container 'mdcl' with payload length = 35 (12 for minm + 14 for asar + 9 for asal)
    const uint8_t raw_daap[] = {
        'm','d','c','l', 0, 0, 0, 35,
            'm','i','n','m', 0, 0, 0, 4, 'S','o','n','g',
            'a','s','a','r', 0, 0, 0, 6, 'A','r','t','i','s','t',
            'a','s','a','l', 0, 0, 0, 1, 'X'
    };

    char title[32] = {0};
    char artist[32] = {0};
    char album[32] = {0};

    TEST_ASSERT_TRUE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, sizeof(title)));
    TEST_ASSERT_EQUAL_STRING("Song", title);

    TEST_ASSERT_TRUE(parse_daap_tag(raw_daap, sizeof(raw_daap), "asar", artist, sizeof(artist)));
    TEST_ASSERT_EQUAL_STRING("Artist", artist);

    TEST_ASSERT_TRUE(parse_daap_tag(raw_daap, sizeof(raw_daap), "asal", album, sizeof(album)));
    TEST_ASSERT_EQUAL_STRING("X", album);
}

void test_daap_deeply_nested_tags(void) {
    // 3 levels of nesting: mlcl -> mlit -> mdcl -> minm
    // minm: 4 + 4 + 4 = 12 bytes
    // mdcl: 4 + 4 + 12 = 20 bytes (payload = 12)
    // mlit: 4 + 4 + 20 = 28 bytes (payload = 20)
    // mlcl: 4 + 4 + 28 = 36 bytes (payload = 28)
    const uint8_t raw_daap[] = {
        'm','l','c','l', 0, 0, 0, 28,
            'm','l','i','t', 0, 0, 0, 20,
                'm','d','c','l', 0, 0, 0, 12,
                    'm','i','n','m', 0, 0, 0, 4, 'D','e','e','p'
    };

    char title[32] = {0};
    TEST_ASSERT_TRUE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, sizeof(title)));
    TEST_ASSERT_EQUAL_STRING("Deep", title);
}

void test_daap_empty_value(void) {
    const uint8_t raw_daap[] = { 'm','i','n','m', 0, 0, 0, 0 };
    char title[32] = "previous_value";

    TEST_ASSERT_TRUE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, sizeof(title)));
    TEST_ASSERT_EQUAL_STRING("", title);
}

void test_daap_truncation_protection(void) {
    const uint8_t raw_daap[] = {
        'a','s','a','r', 0, 0, 0, 20,
        '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I','J'
    };
    char artist[8];
    memset(artist, 0xFF, sizeof(artist));

    TEST_ASSERT_TRUE(parse_daap_tag(raw_daap, sizeof(raw_daap), "asar", artist, sizeof(artist)));
    // Buffer size is 8, so max string length is 7
    TEST_ASSERT_EQUAL_UINT32(7, strlen(artist));
    TEST_ASSERT_EQUAL_STRING("0123456", artist);
    TEST_ASSERT_EQUAL_HEX8('\0', artist[7]);
}

void test_daap_malformed_length_exceeds_buffer(void) {
    // Declares 50 bytes of payload, but only 4 bytes exist after header
    const uint8_t raw_daap[] = { 'm','i','n','m', 0, 0, 0, 50, 'S','o','n','g' };
    char title[32] = "initial";

    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, sizeof(title)));
    TEST_ASSERT_EQUAL_STRING("", title);
}

void test_daap_malformed_overflow_length(void) {
    // 0xFFFFFFFF length should be caught by overflow checks
    const uint8_t raw_daap[] = { 'm','i','n','m', 0xFF, 0xFF, 0xFF, 0xFF, 'S','o','n','g' };
    char title[32] = "initial";

    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, sizeof(title)));
    TEST_ASSERT_EQUAL_STRING("", title);
}

void test_daap_buffer_too_short(void) {
    const uint8_t raw_daap[] = { 'm','i','n','m', 0, 0 }; // Only 6 bytes, need >= 8
    char title[32] = "initial";

    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, sizeof(title)));
    TEST_ASSERT_EQUAL_STRING("", title);
}

void test_daap_null_and_invalid_args(void) {
    const uint8_t raw_daap[] = { 'm','i','n','m', 0, 0, 0, 4, 'S','o','n','g' };
    char title[32] = {0};

    // NULL payload
    TEST_ASSERT_FALSE(parse_daap_tag(NULL, 12, "minm", title, sizeof(title)));

    // NULL output buffer
    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", NULL, sizeof(title)));

    // Zero output length
    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, 0));

    // NULL 4CC
    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), NULL, title, sizeof(title)));

    // 4CC wrong length
    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), "min", title, sizeof(title)));
    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), "minmx", title, sizeof(title)));
    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), "", title, sizeof(title)));
}

void test_daap_tag_not_found(void) {
    const uint8_t raw_daap[] = {
        'm','i','n','m', 0, 0, 0, 4, 'S','o','n','g',
        'a','s','a','r', 0, 0, 0, 6, 'A','r','t','i','s','t'
    };
    char album[32] = "initial";

    TEST_ASSERT_FALSE(parse_daap_tag(raw_daap, sizeof(raw_daap), "asal", album, sizeof(album)));
    TEST_ASSERT_EQUAL_STRING("", album);
}

void test_airplay_status_callback_registration(void) {
    airplay_register_status_callback(on_status_change_test);

    TEST_ASSERT_FALSE(airplay_is_active());
    TEST_ASSERT_EQUAL_INT(0, s_status_cb_count);

    airplay_mock_set_active(true);
    TEST_ASSERT_TRUE(airplay_is_active());
    TEST_ASSERT_TRUE(s_last_active);
    TEST_ASSERT_EQUAL_INT(1, s_status_cb_count);

    // Duplicate active state should not fire duplicate callback
    airplay_mock_set_active(true);
    TEST_ASSERT_EQUAL_INT(1, s_status_cb_count);

    airplay_mock_set_active(false);
    TEST_ASSERT_FALSE(airplay_is_active());
    TEST_ASSERT_FALSE(s_last_active);
    TEST_ASSERT_EQUAL_INT(2, s_status_cb_count);
}

void test_airplay_track_callback_registration(void) {
    airplay_register_track_callback(on_track_change_test);

    TEST_ASSERT_EQUAL_INT(0, s_track_cb_count);

    airplay_mock_trigger_track("Anti-Hero", "Taylor Swift", "Midnights");

    TEST_ASSERT_EQUAL_INT(1, s_track_cb_count);
    TEST_ASSERT_EQUAL_STRING("Anti-Hero", s_last_title);
    TEST_ASSERT_EQUAL_STRING("Taylor Swift", s_last_artist);
    TEST_ASSERT_EQUAL_STRING("Midnights", s_last_album);
}

void test_airplay_feed_daap_metadata(void) {
    airplay_register_track_callback(on_track_change_test);

    const uint8_t raw_daap[] = {
        'm','d','c','l', 0, 0, 0, 51,
            'm','i','n','m', 0, 0, 0, 9, 'A','n','t','i','-','H','e','r','o',
            'a','s','a','r', 0, 0, 0, 12, 'T','a','y','l','o','r',' ','S','w','i','f','t',
            'a','s','a','l', 0, 0, 0, 6, '1','9','8','9','!','!'
    };

    airplay_mock_feed_daap_metadata(raw_daap, sizeof(raw_daap));

    TEST_ASSERT_EQUAL_INT(1, s_track_cb_count);
    TEST_ASSERT_EQUAL_STRING("Anti-Hero", s_last_title);
    TEST_ASSERT_EQUAL_STRING("Taylor Swift", s_last_artist);
    TEST_ASSERT_EQUAL_STRING("1989!!", s_last_album);
}

void test_airplay_feed_audio(void) {
    TEST_ASSERT_TRUE(audio_i2s_init());

    const uint8_t dummy_pcm[128] = { 0x55 };
    // When inactive, stray UDP audio packets are safely ignored
    airplay_mock_set_active(false);
    airplay_mock_feed_audio(dummy_pcm, sizeof(dummy_pcm));

    // When active, incoming PCM frames are forwarded to audio_i2s_write
    airplay_mock_set_active(true);
    airplay_mock_feed_audio(dummy_pcm, sizeof(dummy_pcm));
}

void test_airplay_abnormal_disconnect_resets_active(void) {
    airplay_register_status_callback(on_status_change_test);
    airplay_mock_set_active(true);
    TEST_ASSERT_TRUE(airplay_is_active());
    TEST_ASSERT_EQUAL_INT(1, s_status_cb_count);

    // Abrupt network or client drop resets active state and notifies listener
    airplay_mock_simulate_disconnect();
    TEST_ASSERT_FALSE(airplay_is_active());
    TEST_ASSERT_EQUAL_INT(2, s_status_cb_count);
    TEST_ASSERT_FALSE(s_last_active);
}

void test_airplay_init_stop_state(void) {
    airplay_register_status_callback(on_status_change_test);

    TEST_ASSERT_TRUE(airplay_receiver_init("Custom-AirPlay-Speaker"));
    TEST_ASSERT_FALSE(airplay_is_active());

    airplay_mock_set_active(true);
    TEST_ASSERT_TRUE(airplay_is_active());
    TEST_ASSERT_EQUAL_INT(1, s_status_cb_count);

    airplay_receiver_stop();
    TEST_ASSERT_FALSE(airplay_is_active());
    TEST_ASSERT_EQUAL_INT(2, s_status_cb_count);
    TEST_ASSERT_FALSE(s_last_active);
}

#if defined(ARDUINO)
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_daap_minm_parsing);
    RUN_TEST(test_daap_asar_parsing);
    RUN_TEST(test_daap_asal_parsing);
    RUN_TEST(test_daap_multiple_sibling_tags);
    RUN_TEST(test_daap_nested_tags_in_container);
    RUN_TEST(test_daap_deeply_nested_tags);
    RUN_TEST(test_daap_empty_value);
    RUN_TEST(test_daap_truncation_protection);
    RUN_TEST(test_daap_malformed_length_exceeds_buffer);
    RUN_TEST(test_daap_malformed_overflow_length);
    RUN_TEST(test_daap_buffer_too_short);
    RUN_TEST(test_daap_null_and_invalid_args);
    RUN_TEST(test_daap_tag_not_found);
    RUN_TEST(test_airplay_status_callback_registration);
    RUN_TEST(test_airplay_track_callback_registration);
    RUN_TEST(test_airplay_feed_daap_metadata);
    RUN_TEST(test_airplay_feed_audio);
    RUN_TEST(test_airplay_abnormal_disconnect_resets_active);
    RUN_TEST(test_airplay_init_stop_state);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_daap_minm_parsing);
    RUN_TEST(test_daap_asar_parsing);
    RUN_TEST(test_daap_asal_parsing);
    RUN_TEST(test_daap_multiple_sibling_tags);
    RUN_TEST(test_daap_nested_tags_in_container);
    RUN_TEST(test_daap_deeply_nested_tags);
    RUN_TEST(test_daap_empty_value);
    RUN_TEST(test_daap_truncation_protection);
    RUN_TEST(test_daap_malformed_length_exceeds_buffer);
    RUN_TEST(test_daap_malformed_overflow_length);
    RUN_TEST(test_daap_buffer_too_short);
    RUN_TEST(test_daap_null_and_invalid_args);
    RUN_TEST(test_daap_tag_not_found);
    RUN_TEST(test_airplay_status_callback_registration);
    RUN_TEST(test_airplay_track_callback_registration);
    RUN_TEST(test_airplay_feed_daap_metadata);
    RUN_TEST(test_airplay_feed_audio);
    RUN_TEST(test_airplay_abnormal_disconnect_resets_active);
    RUN_TEST(test_airplay_init_stop_state);
    return UNITY_END();
}
#endif
