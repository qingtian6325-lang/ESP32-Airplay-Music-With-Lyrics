#include <unity.h>
#include <string.h>
#include "bt_sink.h"

static TrackInfo s_last_track;
static int s_track_cb_count = 0;
static bool s_last_playing = false;
static int s_status_cb_count = 0;

static void on_track_change_test(const TrackInfo *track) {
    if (track) {
        s_last_track = *track;
        s_track_cb_count++;
    }
}

static void on_status_change_test(bool is_playing) {
    s_last_playing = is_playing;
    s_status_cb_count++;
}

void setUp(void) {
    memset(&s_last_track, 0, sizeof(s_last_track));
    s_track_cb_count = 0;
    s_last_playing = false;
    s_status_cb_count = 0;
    bt_sink_stop();
}

void tearDown(void) {
}

void test_parse_normal_metadata(void) {
    TrackInfo track;
    memset(&track, 0, sizeof(track));

    parse_avrcp_metadata("Shape of You", "Ed Sheeran", "Divide", &track);

    TEST_ASSERT_EQUAL_STRING("Shape of You", track.title);
    TEST_ASSERT_EQUAL_STRING("Ed Sheeran", track.artist);
    TEST_ASSERT_EQUAL_STRING("Divide", track.album);
    TEST_ASSERT_EQUAL_UINT32(0, track.duration_ms);
}

void test_parse_null_pointers(void) {
    TrackInfo track;
    memset(&track, 0xAA, sizeof(track));

    // Null output pointer should not crash or fault
    parse_avrcp_metadata("Title", "Artist", "Album", NULL);

    // All null inputs
    parse_avrcp_metadata(NULL, NULL, NULL, &track);
    TEST_ASSERT_EQUAL_STRING("", track.title);
    TEST_ASSERT_EQUAL_STRING("", track.artist);
    TEST_ASSERT_EQUAL_STRING("", track.album);
    TEST_ASSERT_EQUAL_UINT32(0, track.duration_ms);

    // Partial null inputs
    parse_avrcp_metadata("Hello", NULL, "World", &track);
    TEST_ASSERT_EQUAL_STRING("Hello", track.title);
    TEST_ASSERT_EQUAL_STRING("", track.artist);
    TEST_ASSERT_EQUAL_STRING("World", track.album);
}

void test_parse_empty_strings(void) {
    TrackInfo track;
    memset(&track, 0, sizeof(track));

    parse_avrcp_metadata("", "", "", &track);
    TEST_ASSERT_EQUAL_STRING("", track.title);
    TEST_ASSERT_EQUAL_STRING("", track.artist);
    TEST_ASSERT_EQUAL_STRING("", track.album);
}

void test_parse_whitespace_trimming(void) {
    TrackInfo track;
    memset(&track, 0, sizeof(track));

    parse_avrcp_metadata("   Blinding Lights   \r\n", "\tThe Weeknd  ", "  After Hours\n", &track);
    TEST_ASSERT_EQUAL_STRING("Blinding Lights", track.title);
    TEST_ASSERT_EQUAL_STRING("The Weeknd", track.artist);
    TEST_ASSERT_EQUAL_STRING("After Hours", track.album);

    // All whitespace string should trim to empty
    parse_avrcp_metadata("   \t  \r\n  ", "   ", " \t ", &track);
    TEST_ASSERT_EQUAL_STRING("", track.title);
    TEST_ASSERT_EQUAL_STRING("", track.artist);
    TEST_ASSERT_EQUAL_STRING("", track.album);

    // Internal whitespace should be preserved
    parse_avrcp_metadata("Song   With   Spaces", "Artist   Name", "Album   Name", &track);
    TEST_ASSERT_EQUAL_STRING("Song   With   Spaces", track.title);
    TEST_ASSERT_EQUAL_STRING("Artist   Name", track.artist);
    TEST_ASSERT_EQUAL_STRING("Album   Name", track.album);
}

void test_parse_truncation_protection(void) {
    TrackInfo track;
    memset(&track, 0, sizeof(track));

    // Create a 200-character string
    char long_string[201];
    for (int i = 0; i < 200; i++) {
        long_string[i] = 'A' + (i % 26);
    }
    long_string[200] = '\0';

    parse_avrcp_metadata(long_string, long_string, long_string, &track);

    // 128-byte buffer can hold max 127 characters + null terminator
    TEST_ASSERT_EQUAL_UINT32(127, strlen(track.title));
    TEST_ASSERT_EQUAL_UINT32(127, strlen(track.artist));
    TEST_ASSERT_EQUAL_UINT32(127, strlen(track.album));
    TEST_ASSERT_EQUAL_HEX8('\0', track.title[127]);
    TEST_ASSERT_EQUAL_HEX8('\0', track.artist[127]);
    TEST_ASSERT_EQUAL_HEX8('\0', track.album[127]);

    // Verify first 127 characters match
    TEST_ASSERT_EQUAL_UINT8_ARRAY(long_string, track.title, 127);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(long_string, track.artist, 127);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(long_string, track.album, 127);
}

void test_three_param_overload(void) {
    TrackInfo track;
    memset(&track, 0, sizeof(track));

    parse_avrcp_metadata("Shape of You", "Ed Sheeran", &track);
    TEST_ASSERT_EQUAL_STRING("Shape of You", track.title);
    TEST_ASSERT_EQUAL_STRING("Ed Sheeran", track.artist);
    TEST_ASSERT_EQUAL_STRING("", track.album);
}

void test_callback_registration_and_dispatch(void) {
    bt_sink_register_track_callback(on_track_change_test);
    bt_sink_register_status_callback(on_status_change_test);

    TrackInfo test_track;
    parse_avrcp_metadata("Hotel California", "Eagles", "Hotel California", &test_track);
    test_track.duration_ms = 391000;

    bt_sink_mock_trigger_track(&test_track);
    TEST_ASSERT_EQUAL_INT(1, s_track_cb_count);
    TEST_ASSERT_EQUAL_STRING("Hotel California", s_last_track.title);
    TEST_ASSERT_EQUAL_STRING("Eagles", s_last_track.artist);
    TEST_ASSERT_EQUAL_STRING("Hotel California", s_last_track.album);
    TEST_ASSERT_EQUAL_UINT32(391000, s_last_track.duration_ms);

    bt_sink_mock_set_playing(true);
    TEST_ASSERT_EQUAL_INT(1, s_status_cb_count);
    TEST_ASSERT_TRUE(s_last_playing);
    TEST_ASSERT_TRUE(bt_sink_is_playing());

    bt_sink_mock_set_playing(false);
    TEST_ASSERT_EQUAL_INT(2, s_status_cb_count);
    TEST_ASSERT_FALSE(s_last_playing);
    TEST_ASSERT_FALSE(bt_sink_is_playing());
}

void test_sink_init_stop_state(void) {
    TEST_ASSERT_FALSE(bt_sink_is_connected());
    TEST_ASSERT_FALSE(bt_sink_is_playing());

    TEST_ASSERT_TRUE(bt_sink_init("Custom-Speaker"));

    bt_sink_mock_set_connected(true);
    TEST_ASSERT_TRUE(bt_sink_is_connected());

    bt_sink_stop();
    TEST_ASSERT_FALSE(bt_sink_is_connected());
    TEST_ASSERT_FALSE(bt_sink_is_playing());
}

#if defined(ARDUINO)
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_parse_normal_metadata);
    RUN_TEST(test_parse_null_pointers);
    RUN_TEST(test_parse_empty_strings);
    RUN_TEST(test_parse_whitespace_trimming);
    RUN_TEST(test_parse_truncation_protection);
    RUN_TEST(test_three_param_overload);
    RUN_TEST(test_callback_registration_and_dispatch);
    RUN_TEST(test_sink_init_stop_state);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_normal_metadata);
    RUN_TEST(test_parse_null_pointers);
    RUN_TEST(test_parse_empty_strings);
    RUN_TEST(test_parse_whitespace_trimming);
    RUN_TEST(test_parse_truncation_protection);
    RUN_TEST(test_three_param_overload);
    RUN_TEST(test_callback_registration_and_dispatch);
    RUN_TEST(test_sink_init_stop_state);
    return UNITY_END();
}
#endif
