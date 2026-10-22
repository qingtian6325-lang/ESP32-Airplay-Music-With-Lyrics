#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "lyrics_service.h"

static LyricsData s_received_lyrics;
static bool s_received_success = false;
static int s_callback_invocations = 0;

static void on_lyrics_loaded_test(const LyricsData *lyrics, bool success) {
    if (lyrics) {
        memcpy(&s_received_lyrics, lyrics, sizeof(LyricsData));
    } else {
        memset(&s_received_lyrics, 0, sizeof(LyricsData));
    }
    s_received_success = success;
    s_callback_invocations++;
}

void setUp(void) {
    memset(&s_received_lyrics, 0, sizeof(LyricsData));
    s_received_success = false;
    s_callback_invocations = 0;
#if !(defined(ESP_PLATFORM) || defined(ARDUINO)) || defined(ARDUINO_ARCH_NATIVE)
    lyrics_mock_reset();
#endif
}

void tearDown(void) {
}

// 1. Timestamp parsing [mm:ss.xx] (hundredths of a second)
void test_lrc_timestamp_parsing_two_decimal(void) {
    const char *sample = "[00:15.30]First line\n[00:20.50]Second line\n[01:05.00]Third line\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));

    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));
    TEST_ASSERT_EQUAL_UINT32(3, lrc.count);
    TEST_ASSERT_TRUE(lrc.loaded);

    TEST_ASSERT_EQUAL_UINT32(15300, lrc.lines[0].time_ms);
    TEST_ASSERT_EQUAL_STRING("First line", lrc.lines[0].text);

    TEST_ASSERT_EQUAL_UINT32(20500, lrc.lines[1].time_ms);
    TEST_ASSERT_EQUAL_STRING("Second line", lrc.lines[1].text);

    TEST_ASSERT_EQUAL_UINT32(65000, lrc.lines[2].time_ms);
    TEST_ASSERT_EQUAL_STRING("Third line", lrc.lines[2].text);
}

// 2. Timestamp parsing [mm:ss.xxx] (milliseconds)
void test_lrc_timestamp_parsing_three_decimal(void) {
    const char *sample = "[00:04.250]Intro starts\n[01:02.050]Verse begins\n[02:15.999]Guitar solo\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));

    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));
    TEST_ASSERT_EQUAL_UINT32(3, lrc.count);

    TEST_ASSERT_EQUAL_UINT32(4250, lrc.lines[0].time_ms);
    TEST_ASSERT_EQUAL_STRING("Intro starts", lrc.lines[0].text);

    TEST_ASSERT_EQUAL_UINT32(62050, lrc.lines[1].time_ms);
    TEST_ASSERT_EQUAL_STRING("Verse begins", lrc.lines[1].text);

    TEST_ASSERT_EQUAL_UINT32(135999, lrc.lines[2].time_ms);
    TEST_ASSERT_EQUAL_STRING("Guitar solo", lrc.lines[2].text);
}

// 3. Multi-timestamps on a single line
void test_lrc_multi_timestamps_single_line(void) {
    const char *sample = "[00:10.00][00:30.00][01:15.00]Chorus: Never gonna give you up\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));

    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));
    TEST_ASSERT_EQUAL_UINT32(3, lrc.count);

    TEST_ASSERT_EQUAL_UINT32(10000, lrc.lines[0].time_ms);
    TEST_ASSERT_EQUAL_STRING("Chorus: Never gonna give you up", lrc.lines[0].text);

    TEST_ASSERT_EQUAL_UINT32(30000, lrc.lines[1].time_ms);
    TEST_ASSERT_EQUAL_STRING("Chorus: Never gonna give you up", lrc.lines[1].text);

    TEST_ASSERT_EQUAL_UINT32(75000, lrc.lines[2].time_ms);
    TEST_ASSERT_EQUAL_STRING("Chorus: Never gonna give you up", lrc.lines[2].text);
}

// 4. Out-of-order lines must be sorted ascending by timestamp
void test_lrc_out_of_order_sorting(void) {
    const char *sample = 
        "[01:30.00]Line 3 (90s)\n"
        "[00:15.00]Line 1 (15s)\n"
        "[00:45.00]Line 2 (45s)\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));

    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));
    TEST_ASSERT_EQUAL_UINT32(3, lrc.count);

    TEST_ASSERT_EQUAL_UINT32(15000, lrc.lines[0].time_ms);
    TEST_ASSERT_EQUAL_STRING("Line 1 (15s)", lrc.lines[0].text);

    TEST_ASSERT_EQUAL_UINT32(45000, lrc.lines[1].time_ms);
    TEST_ASSERT_EQUAL_STRING("Line 2 (45s)", lrc.lines[1].text);

    TEST_ASSERT_EQUAL_UINT32(90000, lrc.lines[2].time_ms);
    TEST_ASSERT_EQUAL_STRING("Line 3 (90s)", lrc.lines[2].text);
}

// 5. Header stripping [ti:...], [ar:...], [al:...], [offset:...]
void test_lrc_header_stripping(void) {
    const char *sample = 
        "[ti:Bohemian Rhapsody]\n"
        "[ar:Queen]\n"
        "[al:A Night at the Opera]\n"
        "[by:Someone]\n"
        "[offset:+500]\n"
        "[00:01.00]Is this the real life?\n"
        "[00:05.50]Is this just fantasy?\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));

    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));
    TEST_ASSERT_EQUAL_UINT32(2, lrc.count);
    TEST_ASSERT_EQUAL_STRING("Bohemian Rhapsody", lrc.title);
    TEST_ASSERT_EQUAL_STRING("Queen", lrc.artist);

    TEST_ASSERT_EQUAL_UINT32(1000, lrc.lines[0].time_ms);
    TEST_ASSERT_EQUAL_STRING("Is this the real life?", lrc.lines[0].text);
    TEST_ASSERT_EQUAL_UINT32(5500, lrc.lines[1].time_ms);
    TEST_ASSERT_EQUAL_STRING("Is this just fantasy?", lrc.lines[1].text);
}

// 6. Binary search lookup (lrc_get_active_index_at)
void test_lrc_binary_search_active_index(void) {
    const char *sample = 
        "[00:10.00]Line 0 (10s)\n"
        "[00:20.00]Line 1 (20s)\n"
        "[00:30.00]Line 2 (30s)\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));
    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));

    // Before first line (ms < 10000) -> -1
    TEST_ASSERT_EQUAL_INT(-1, lrc_get_active_index_at(&lrc, 0));
    TEST_ASSERT_EQUAL_INT(-1, lrc_get_active_index_at(&lrc, 5000));
    TEST_ASSERT_EQUAL_INT(-1, lrc_get_active_index_at(&lrc, 9999));

    // At first line
    TEST_ASSERT_EQUAL_INT(0, lrc_get_active_index_at(&lrc, 10000));
    TEST_ASSERT_EQUAL_INT(0, lrc_get_active_index_at(&lrc, 15000));
    TEST_ASSERT_EQUAL_INT(0, lrc_get_active_index_at(&lrc, 19999));

    // At second line
    TEST_ASSERT_EQUAL_INT(1, lrc_get_active_index_at(&lrc, 20000));
    TEST_ASSERT_EQUAL_INT(1, lrc_get_active_index_at(&lrc, 25000));
    TEST_ASSERT_EQUAL_INT(1, lrc_get_active_index_at(&lrc, 29999));

    // At third line (last line)
    TEST_ASSERT_EQUAL_INT(2, lrc_get_active_index_at(&lrc, 30000));
    TEST_ASSERT_EQUAL_INT(2, lrc_get_active_index_at(&lrc, 50000));
    TEST_ASSERT_EQUAL_INT(2, lrc_get_active_index_at(&lrc, 3600000));
}

// 7. Binary search with offsets -2 to +2
void test_lrc_get_line_at_offsets(void) {
    const char *sample = 
        "[00:10.00]Line 0 (10s)\n"
        "[00:20.00]Line 1 (20s)\n"
        "[00:30.00]Line 2 (30s)\n"
        "[00:40.00]Line 3 (40s)\n"
        "[00:50.00]Line 4 (50s)\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));
    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));

    // At 35s, active is Line 2 (index 2)
    TEST_ASSERT_EQUAL_STRING("Line 0 (10s)", lrc_get_line_at(&lrc, 35000, -2));
    TEST_ASSERT_EQUAL_STRING("Line 1 (20s)", lrc_get_line_at(&lrc, 35000, -1));
    TEST_ASSERT_EQUAL_STRING("Line 2 (30s)", lrc_get_line_at(&lrc, 35000, 0));
    TEST_ASSERT_EQUAL_STRING("Line 3 (40s)", lrc_get_line_at(&lrc, 35000, 1));
    TEST_ASSERT_EQUAL_STRING("Line 4 (50s)", lrc_get_line_at(&lrc, 35000, 2));

    // At 15s, active is Line 0 (index 0)
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 15000, -2)); // out of bounds
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 15000, -1)); // out of bounds
    TEST_ASSERT_EQUAL_STRING("Line 0 (10s)", lrc_get_line_at(&lrc, 15000, 0));
    TEST_ASSERT_EQUAL_STRING("Line 1 (20s)", lrc_get_line_at(&lrc, 15000, 1));
    TEST_ASSERT_EQUAL_STRING("Line 2 (30s)", lrc_get_line_at(&lrc, 15000, 2));
}

// 8. Boundary conditions before first and after last line
void test_lrc_boundary_before_first_and_after_last(void) {
    const char *sample = 
        "[00:10.00]First line\n"
        "[00:20.00]Second line\n"
        "[00:30.00]Last line\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));
    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));

    // Before first line (5s) -> active index is -1
    // Offset 0 (current) is not started yet -> empty string
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 5000, -2));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 5000, -1));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 5000, 0));
    // Upcoming lines
    TEST_ASSERT_EQUAL_STRING("First line", lrc_get_line_at(&lrc, 5000, 1));
    TEST_ASSERT_EQUAL_STRING("Second line", lrc_get_line_at(&lrc, 5000, 2));

    // After last line (60s) -> active index is 2 (Last line)
    TEST_ASSERT_EQUAL_STRING("First line", lrc_get_line_at(&lrc, 60000, -2));
    TEST_ASSERT_EQUAL_STRING("Second line", lrc_get_line_at(&lrc, 60000, -1));
    TEST_ASSERT_EQUAL_STRING("Last line", lrc_get_line_at(&lrc, 60000, 0));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 60000, 1));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 60000, 2));
}

// 9. Buffer truncation safety for lines and counts
void test_lrc_buffer_truncation_safety(void) {
    char big_sample[1024];
    char long_text[200];
    memset(long_text, 'X', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';

    snprintf(big_sample, sizeof(big_sample), "[00:10.00]%s\n", long_text);

    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));
    TEST_ASSERT_TRUE(lrc_parse(big_sample, &lrc));
    TEST_ASSERT_EQUAL_UINT32(1, lrc.count);
    // Line text must be truncated to MAX_LRC_LINE_LEN - 1 (127 characters)
    TEST_ASSERT_EQUAL_INT(MAX_LRC_LINE_LEN - 1, strlen(lrc.lines[0].text));
    TEST_ASSERT_EQUAL_CHAR('X', lrc.lines[0].text[0]);
    TEST_ASSERT_EQUAL_CHAR('X', lrc.lines[0].text[MAX_LRC_LINE_LEN - 2]);
    TEST_ASSERT_EQUAL_CHAR('\0', lrc.lines[0].text[MAX_LRC_LINE_LEN - 1]);

    // Test exceeding MAX_LRC_LINES (250)
    // Allocate 300 lines dynamically on heap to avoid huge stack
    char *many_lines = (char *)malloc(300 * 32);
    TEST_ASSERT_NOT_NULL(many_lines);
    many_lines[0] = '\0';
    for (int i = 0; i < 300; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "[%02d:%02d.00]Line %d\n", i / 60, i % 60, i);
        strcat(many_lines, buf);
    }

    TEST_ASSERT_TRUE(lrc_parse(many_lines, &lrc));
    // Count must be capped at MAX_LRC_LINES
    TEST_ASSERT_EQUAL_UINT32(MAX_LRC_LINES, lrc.count);
    free(many_lines);
}

// 10. Null and malformed inputs
void test_lrc_null_and_malformed_inputs(void) {
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));

    TEST_ASSERT_FALSE(lrc_parse(NULL, &lrc));
    TEST_ASSERT_FALSE(lrc_parse("", &lrc));
    TEST_ASSERT_FALSE(lrc_parse("   \n\r\n   ", &lrc));
    TEST_ASSERT_FALSE(lrc_parse("[ti:No Timestamps]", &lrc));
    TEST_ASSERT_FALSE(lrc_parse("[invalid:tag]Just text", &lrc));
    TEST_ASSERT_FALSE(lrc_parse(NULL, NULL));

    // Lookup functions on null or unloaded data
    TEST_ASSERT_EQUAL_INT(-1, lrc_get_active_index_at(NULL, 1000));
    TEST_ASSERT_EQUAL_INT(-1, lrc_get_active_index_at(&lrc, 1000));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(NULL, 1000, 0));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 1000, 0));
}

// 11. URL encode helper
void test_url_encode(void) {
    char out[128];

    url_encode("Hello World", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Hello%20World", out);

    url_encode("AC/DC & Guns N' Roses", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("AC%2FDC%20%26%20Guns%20N%27%20Roses", out);

    url_encode("Simple-Track_01.mp3~v2", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Simple-Track_01.mp3~v2", out);

    // Truncation check
    char small[8];
    url_encode("Very Long Text", small, sizeof(small));
    TEST_ASSERT_EQUAL_INT(sizeof(small) - 1, strlen(small));
    TEST_ASSERT_EQUAL_CHAR('\0', small[sizeof(small) - 1]);

    // Null safety
    url_encode(NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// 12. Async Lyrics Fetch Mocking (Native)
void test_lyrics_service_async_mock(void) {
    TEST_ASSERT_TRUE(lyrics_service_init());

    const char *mock_lrc = 
        "[ti:Yesterday]\n"
        "[ar:The Beatles]\n"
        "[00:03.00]Yesterday, all my troubles seemed so far away\n"
        "[00:09.00]Now it looks as though they're here to stay\n";

    lyrics_mock_set_response(mock_lrc, true);

    lyrics_fetch_async("Yesterday", "The Beatles", on_lyrics_loaded_test);

    TEST_ASSERT_EQUAL_INT(1, s_callback_invocations);
    TEST_ASSERT_TRUE(s_received_success);
    TEST_ASSERT_TRUE(s_received_lyrics.loaded);
    TEST_ASSERT_EQUAL_UINT32(2, s_received_lyrics.count);
    TEST_ASSERT_EQUAL_STRING("Yesterday", s_received_lyrics.title);
    TEST_ASSERT_EQUAL_STRING("The Beatles", s_received_lyrics.artist);
    TEST_ASSERT_EQUAL_STRING("Yesterday, all my troubles seemed so far away", s_received_lyrics.lines[0].text);

    lyrics_service_stop();
}

// 13. Async Lyrics Fetch Failure Mocking
void test_lyrics_service_async_mock_failure(void) {
    TEST_ASSERT_TRUE(lyrics_service_init());

    lyrics_mock_set_response(NULL, false);

    lyrics_fetch_async("Unknown Song", "Unknown Artist", on_lyrics_loaded_test);

    TEST_ASSERT_EQUAL_INT(1, s_callback_invocations);
    TEST_ASSERT_FALSE(s_received_success);
    TEST_ASSERT_FALSE(s_received_lyrics.loaded);
    TEST_ASSERT_EQUAL_STRING("Unknown Song", s_received_lyrics.title);
    TEST_ASSERT_EQUAL_STRING("Unknown Artist", s_received_lyrics.artist);

    lyrics_service_stop();
}

#if defined(ARDUINO)
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_lrc_timestamp_parsing_two_decimal);
    RUN_TEST(test_lrc_timestamp_parsing_three_decimal);
    RUN_TEST(test_lrc_multi_timestamps_single_line);
    RUN_TEST(test_lrc_out_of_order_sorting);
    RUN_TEST(test_lrc_header_stripping);
    RUN_TEST(test_lrc_binary_search_active_index);
    RUN_TEST(test_lrc_get_line_at_offsets);
    RUN_TEST(test_lrc_boundary_before_first_and_after_last);
    RUN_TEST(test_lrc_buffer_truncation_safety);
    RUN_TEST(test_lrc_null_and_malformed_inputs);
    RUN_TEST(test_url_encode);
    RUN_TEST(test_lyrics_service_async_mock);
    RUN_TEST(test_lyrics_service_async_mock_failure);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_lrc_timestamp_parsing_two_decimal);
    RUN_TEST(test_lrc_timestamp_parsing_three_decimal);
    RUN_TEST(test_lrc_multi_timestamps_single_line);
    RUN_TEST(test_lrc_out_of_order_sorting);
    RUN_TEST(test_lrc_header_stripping);
    RUN_TEST(test_lrc_binary_search_active_index);
    RUN_TEST(test_lrc_get_line_at_offsets);
    RUN_TEST(test_lrc_boundary_before_first_and_after_last);
    RUN_TEST(test_lrc_buffer_truncation_safety);
    RUN_TEST(test_lrc_null_and_malformed_inputs);
    RUN_TEST(test_url_encode);
    RUN_TEST(test_lyrics_service_async_mock);
    RUN_TEST(test_lyrics_service_async_mock_failure);
    return UNITY_END();
}
#endif
