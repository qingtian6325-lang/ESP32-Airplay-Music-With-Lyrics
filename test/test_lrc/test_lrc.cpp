#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "lyrics_service.h"
#include "config.h"

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

    // At exact timestamps
    TEST_ASSERT_EQUAL_INT(0, lrc_get_active_index_at(&lrc, 10000));
    TEST_ASSERT_EQUAL_INT(1, lrc_get_active_index_at(&lrc, 20000));
    TEST_ASSERT_EQUAL_INT(2, lrc_get_active_index_at(&lrc, 30000));

    // Between timestamps
    TEST_ASSERT_EQUAL_INT(0, lrc_get_active_index_at(&lrc, 15000));
    TEST_ASSERT_EQUAL_INT(1, lrc_get_active_index_at(&lrc, 25000));

    // Past last line
    TEST_ASSERT_EQUAL_INT(2, lrc_get_active_index_at(&lrc, 35000));
    TEST_ASSERT_EQUAL_INT(2, lrc_get_active_index_at(&lrc, 1000000));
}

// 7. Get line at relative offsets (-2 to +2)
void test_lrc_get_line_at_offsets(void) {
    const char *sample = 
        "[00:10.00]Line 0\n"
        "[00:20.00]Line 1\n"
        "[00:30.00]Line 2\n"
        "[00:40.00]Line 3\n"
        "[00:50.00]Line 4\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));
    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));

    // Active is Line 2 (ms = 35000)
    uint32_t ms = 35000;
    TEST_ASSERT_EQUAL_STRING("Line 2", lrc_get_line_at(&lrc, ms, 0));
    TEST_ASSERT_EQUAL_STRING("Line 1", lrc_get_line_at(&lrc, ms, -1));
    TEST_ASSERT_EQUAL_STRING("Line 0", lrc_get_line_at(&lrc, ms, -2));
    TEST_ASSERT_EQUAL_STRING("Line 3", lrc_get_line_at(&lrc, ms, 1));
    TEST_ASSERT_EQUAL_STRING("Line 4", lrc_get_line_at(&lrc, ms, 2));

    // Out of bounds offsets -> return empty string ""
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, ms, -3));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, ms, 3));
}

// 8. Boundary before first line and after last line
void test_lrc_boundary_before_first_and_after_last(void) {
    const char *sample = 
        "[00:10.00]First line\n"
        "[00:20.00]Second line\n";
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));
    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));

    // Before first line (active is -1)
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 5000, 0));
    TEST_ASSERT_EQUAL_STRING("First line", lrc_get_line_at(&lrc, 5000, 1));
    TEST_ASSERT_EQUAL_STRING("Second line", lrc_get_line_at(&lrc, 5000, 2));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 5000, -1));

    // Past last line (active is 1)
    TEST_ASSERT_EQUAL_STRING("Second line", lrc_get_line_at(&lrc, 50000, 0));
    TEST_ASSERT_EQUAL_STRING("First line", lrc_get_line_at(&lrc, 50000, -1));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(&lrc, 50000, 1));
}

// 9. Buffer overflow and boundary truncation safety
void test_lrc_buffer_truncation_safety(void) {
    char long_line[300];
    memset(long_line, 'A', sizeof(long_line));
    long_line[299] = '\0';

    char sample[400];
    snprintf(sample, sizeof(sample), "[00:10.00]%s\n", long_line);

    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));
    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));
    TEST_ASSERT_EQUAL_UINT32(1, lrc.count);

    // Text must be truncated to MAX_LRC_LINE_LEN - 1 and null-terminated
    TEST_ASSERT_EQUAL_INT(MAX_LRC_LINE_LEN - 1, strlen(lrc.lines[0].text));
    TEST_ASSERT_EQUAL_CHAR('\0', lrc.lines[0].text[MAX_LRC_LINE_LEN - 1]);
}

// 10. Null and malformed input handling
void test_lrc_null_and_malformed_inputs(void) {
    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));

    // Null checks
    TEST_ASSERT_FALSE(lrc_parse(NULL, &lrc));
    TEST_ASSERT_FALSE(lrc_parse("valid text", NULL));
    TEST_ASSERT_EQUAL_INT(-1, lrc_get_active_index_at(NULL, 1000));
    TEST_ASSERT_EQUAL_STRING("", lrc_get_line_at(NULL, 1000, 0));

    // Malformed timestamp without digits or brackets
    TEST_ASSERT_FALSE(lrc_parse("[invalid]Just text", &lrc));
    TEST_ASSERT_EQUAL_UINT32(0, lrc.count);

    // Empty string
    TEST_ASSERT_FALSE(lrc_parse("", &lrc));
    TEST_ASSERT_EQUAL_UINT32(0, lrc.count);
}

// 11. URL encoding helper
void test_url_encode(void) {
    char dst[128];

    // Standard unreserved characters
    url_encode("Hello123", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("Hello123", dst);

    // Spaces and symbols
    url_encode("Hotel California (Live)", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("Hotel%20California%20%28Live%29", dst);

    // Ampersand and plus
    url_encode("Simon & Garfunkel", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("Simon%20%26%20Garfunkel", dst);

    // Small destination buffer safety
    char tiny[6];
    url_encode("A B C", tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_INT(tiny[sizeof(tiny) - 1], '\0');
}

// 12. Lyrics service async request (mock host test)
void test_lyrics_service_async_mock(void) {
    TEST_ASSERT_TRUE(lyrics_service_init());

    const char *sample = 
        "[00:05.00]Mock Line 1\n"
        "[00:15.00]Mock Line 2\n";

    lyrics_mock_set_response(sample, true);

    lyrics_fetch_async("Song Title", "Artist Name", on_lyrics_loaded_test);

    TEST_ASSERT_EQUAL_INT(1, s_callback_invocations);
    TEST_ASSERT_TRUE(s_received_success);
    TEST_ASSERT_TRUE(s_received_lyrics.loaded);
    TEST_ASSERT_EQUAL_UINT32(2, s_received_lyrics.count);
    TEST_ASSERT_EQUAL_STRING("Song Title", s_received_lyrics.title);
    TEST_ASSERT_EQUAL_STRING("Artist Name", s_received_lyrics.artist);
    TEST_ASSERT_EQUAL_STRING("Mock Line 1", s_received_lyrics.lines[0].text);

    lyrics_service_stop();
}

// 13. Lyrics service async request failure (mock host test)
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

// 14. Uninitialized LyricsData passed to lrc_parse with valid tags
void test_lrc_parse_uninitialized_struct_tag_extraction(void) {
    LyricsData uninitialized;
    // Fill with garbage non-zero bytes to simulate dirty stack / uninitialized memory
    memset(&uninitialized, 0xAA, sizeof(uninitialized));

    const char *sample = 
        "[ti:Dirty Stack Song]\n"
        "[ar:Stack Overflow]\n"
        "[00:05.00]Clean lyric line\n";

    TEST_ASSERT_TRUE(lrc_parse(sample, &uninitialized));
    TEST_ASSERT_TRUE(uninitialized.loaded);
    TEST_ASSERT_EQUAL_UINT32(1, uninitialized.count);
    TEST_ASSERT_EQUAL_STRING("Dirty Stack Song", uninitialized.title);
    TEST_ASSERT_EQUAL_STRING("Stack Overflow", uninitialized.artist);
    TEST_ASSERT_EQUAL_STRING("Clean lyric line", uninitialized.lines[0].text);
}

// 15. Uninitialized LyricsData passed to lrc_parse without tags
void test_lrc_parse_uninitialized_struct_no_tags(void) {
    LyricsData uninitialized;
    memset(&uninitialized, 0xBB, sizeof(uninitialized));

    const char *sample = "[00:05.00]Line without header tags\n";

    TEST_ASSERT_TRUE(lrc_parse(sample, &uninitialized));
    TEST_ASSERT_TRUE(uninitialized.loaded);
    TEST_ASSERT_EQUAL_UINT32(1, uninitialized.count);
    TEST_ASSERT_EQUAL_CHAR('\0', uninitialized.title[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', uninitialized.artist[0]);
    TEST_ASSERT_EQUAL_STRING("Line without header tags", uninitialized.lines[0].text);
}

// 16. Stopped lyrics service callback behavior
void test_lyrics_service_stopped_service_callback(void) {
    lyrics_service_stop(); // Ensure service is stopped

    lyrics_fetch_async("Stopped Title", "Stopped Artist", on_lyrics_loaded_test);

    TEST_ASSERT_EQUAL_INT(1, s_callback_invocations);
    TEST_ASSERT_FALSE(s_received_success);
    TEST_ASSERT_FALSE(s_received_lyrics.loaded);
    TEST_ASSERT_EQUAL_STRING("Stopped Title", s_received_lyrics.title);
    TEST_ASSERT_EQUAL_STRING("Stopped Artist", s_received_lyrics.artist);
}

// 17. LRC header tags override request metadata
void test_lyrics_service_lrc_tags_override_request_meta(void) {
    TEST_ASSERT_TRUE(lyrics_service_init());

    const char *sample = 
        "[ti:Tag Title]\n"
        "[ar:Tag Artist]\n"
        "[00:10.00]Song lyric line\n";

    lyrics_mock_set_response(sample, true);

    lyrics_fetch_async("Request Title", "Request Artist", on_lyrics_loaded_test);

    TEST_ASSERT_EQUAL_INT(1, s_callback_invocations);
    TEST_ASSERT_TRUE(s_received_success);
    TEST_ASSERT_EQUAL_STRING("Tag Title", s_received_lyrics.title);
    TEST_ASSERT_EQUAL_STRING("Tag Artist", s_received_lyrics.artist);

    lyrics_service_stop();
}

// 18. Fallback to request metadata when no tags are present
void test_lyrics_service_fallback_to_request_meta_when_no_tags(void) {
    TEST_ASSERT_TRUE(lyrics_service_init());

    const char *sample = "[00:10.00]Lyrics without tags\n";

    lyrics_mock_set_response(sample, true);

    lyrics_fetch_async("Fallback Title", "Fallback Artist", on_lyrics_loaded_test);

    TEST_ASSERT_EQUAL_INT(1, s_callback_invocations);
    TEST_ASSERT_TRUE(s_received_success);
    TEST_ASSERT_EQUAL_STRING("Fallback Title", s_received_lyrics.title);
    TEST_ASSERT_EQUAL_STRING("Fallback Artist", s_received_lyrics.artist);

    lyrics_service_stop();
}

// 19. Real lrc.cx response format parsing with Chinese and millisecond timestamps
void test_lrccx_format_parsing_with_chinese_and_metadata(void) {
    const char *lrccx_response = 
        "[ti:青花瓷]\n"
        "[ar:周杰伦]\n"
        "[al:我很忙]\n"
        "[by:]\n"
        "[offset:0]\n"
        "[00:21.606]素胚勾勒出青花笔锋浓转淡\n"
        "[00:26.130]瓶身描绘的牡丹一如你初妆\n"
        "[00:30.577]冉冉檀香透过窗心事我了然\n"
        "[00:34.999]宣纸上 走笔至此搁一半\n";

    LyricsData lrc;
    memset(&lrc, 0, sizeof(lrc));

    TEST_ASSERT_TRUE(lrc_parse(lrccx_response, &lrc));
    TEST_ASSERT_EQUAL_UINT32(4, lrc.count);
    TEST_ASSERT_TRUE(lrc.loaded);
    TEST_ASSERT_EQUAL_STRING("青花瓷", lrc.title);
    TEST_ASSERT_EQUAL_STRING("周杰伦", lrc.artist);

    TEST_ASSERT_EQUAL_UINT32(21606, lrc.lines[0].time_ms);
    TEST_ASSERT_EQUAL_STRING("素胚勾勒出青花笔锋浓转淡", lrc.lines[0].text);

    TEST_ASSERT_EQUAL_UINT32(26130, lrc.lines[1].time_ms);
    TEST_ASSERT_EQUAL_STRING("瓶身描绘的牡丹一如你初妆", lrc.lines[1].text);

    TEST_ASSERT_EQUAL_UINT32(30577, lrc.lines[2].time_ms);
    TEST_ASSERT_EQUAL_STRING("冉冉檀香透过窗心事我了然", lrc.lines[2].text);

    TEST_ASSERT_EQUAL_UINT32(34999, lrc.lines[3].time_ms);
    TEST_ASSERT_EQUAL_STRING("宣纸上 走笔至此搁一半", lrc.lines[3].text);
}

// 20. Dual-provider: Primary lrc.cx succeeds -> No fallback
void test_lyrics_dual_provider_lrccx_primary_hit(void) {
    TEST_ASSERT_TRUE(lyrics_service_init());

    const char *sample_lrccx = 
        "[ti:晴天]\n"
        "[ar:周杰伦]\n"
        "[00:29.188]故事的小黄花\n"
        "[00:32.612]从出生那年就飘着\n";

    lyrics_mock_set_provider_response(LYRICS_PROVIDER_LRCCX, sample_lrccx, true);
    lyrics_mock_set_provider_response(LYRICS_PROVIDER_LRCLIB, "[00:01.00]Never called\n", false);

    lyrics_fetch_async("晴天", "周杰伦", on_lyrics_loaded_test);

    TEST_ASSERT_EQUAL_INT(1, s_callback_invocations);
    TEST_ASSERT_TRUE(s_received_success);
    TEST_ASSERT_EQUAL_INT(LYRICS_PROVIDER_LRCCX, lyrics_mock_get_last_queried_provider());
    TEST_ASSERT_EQUAL_STRING("晴天", s_received_lyrics.title);
    TEST_ASSERT_EQUAL_STRING("周杰伦", s_received_lyrics.artist);
    TEST_ASSERT_EQUAL_UINT32(2, s_received_lyrics.count);
    TEST_ASSERT_EQUAL_STRING("故事的小黄花", s_received_lyrics.lines[0].text);

    lyrics_service_stop();
}

// 21. Dual-provider: Primary lrc.cx fails (404/empty) -> Automatic fallback to LRCLIB
void test_lyrics_dual_provider_fallback_to_lrclib(void) {
    TEST_ASSERT_TRUE(lyrics_service_init());

    const char *sample_lrclib = 
        "[ti:Western Track]\n"
        "[ar:Indie Artist]\n"
        "[00:12.500]Fallback lyrics line\n";

    // Simulate lrc.cx missing the song (404 / failed)
    lyrics_mock_set_provider_response(LYRICS_PROVIDER_LRCCX, NULL, false);
    // LRCLIB has the song
    lyrics_mock_set_provider_response(LYRICS_PROVIDER_LRCLIB, sample_lrclib, true);

    lyrics_fetch_async("Western Track", "Indie Artist", on_lyrics_loaded_test);

    TEST_ASSERT_EQUAL_INT(1, s_callback_invocations);
    TEST_ASSERT_TRUE(s_received_success);
    // Provider queried should be LRCLIB (secondary fallback)
    TEST_ASSERT_EQUAL_INT(LYRICS_PROVIDER_LRCLIB, lyrics_mock_get_last_queried_provider());
    TEST_ASSERT_EQUAL_STRING("Western Track", s_received_lyrics.title);
    TEST_ASSERT_EQUAL_STRING("Indie Artist", s_received_lyrics.artist);
    TEST_ASSERT_EQUAL_UINT32(1, s_received_lyrics.count);
    TEST_ASSERT_EQUAL_STRING("Fallback lyrics line", s_received_lyrics.lines[0].text);

    lyrics_service_stop();
}

// 22. Dual-provider: Both providers fail -> callback invoked with success=false
void test_lyrics_dual_provider_both_fail(void) {
    TEST_ASSERT_TRUE(lyrics_service_init());

    lyrics_mock_set_provider_response(LYRICS_PROVIDER_LRCCX, NULL, false);
    lyrics_mock_set_provider_response(LYRICS_PROVIDER_LRCLIB, NULL, false);

    lyrics_fetch_async("Nonexistent Song", "Unknown Artist", on_lyrics_loaded_test);

    TEST_ASSERT_EQUAL_INT(1, s_callback_invocations);
    TEST_ASSERT_FALSE(s_received_success);
    TEST_ASSERT_FALSE(s_received_lyrics.loaded);
    TEST_ASSERT_EQUAL_STRING("Nonexistent Song", s_received_lyrics.title);
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
    RUN_TEST(test_lrc_parse_uninitialized_struct_tag_extraction);
    RUN_TEST(test_lrc_parse_uninitialized_struct_no_tags);
    RUN_TEST(test_lyrics_service_stopped_service_callback);
    RUN_TEST(test_lyrics_service_lrc_tags_override_request_meta);
    RUN_TEST(test_lyrics_service_fallback_to_request_meta_when_no_tags);
    RUN_TEST(test_lrccx_format_parsing_with_chinese_and_metadata);
    RUN_TEST(test_lyrics_dual_provider_lrccx_primary_hit);
    RUN_TEST(test_lyrics_dual_provider_fallback_to_lrclib);
    RUN_TEST(test_lyrics_dual_provider_both_fail);
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
    RUN_TEST(test_lrc_parse_uninitialized_struct_tag_extraction);
    RUN_TEST(test_lrc_parse_uninitialized_struct_no_tags);
    RUN_TEST(test_lyrics_service_stopped_service_callback);
    RUN_TEST(test_lyrics_service_lrc_tags_override_request_meta);
    RUN_TEST(test_lyrics_service_fallback_to_request_meta_when_no_tags);
    RUN_TEST(test_lrccx_format_parsing_with_chinese_and_metadata);
    RUN_TEST(test_lyrics_dual_provider_lrccx_primary_hit);
    RUN_TEST(test_lyrics_dual_provider_fallback_to_lrclib);
    RUN_TEST(test_lyrics_dual_provider_both_fail);
    return UNITY_END();
}
#endif
