#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "ui_display.h"
#include "lyrics_service.h"

static LyricsData s_sample_lyrics;

static void setup_sample_lyrics(void) {
    const char *sample =
        "[ti:Sample Song]\n"
        "[ar:Sample Artist]\n"
        "[00:10.00]Line 0 Intro Ends\n"
        "[00:20.00]Line 1 Verse 1\n"
        "[00:30.00]Line 2 Verse 2\n"
        "[00:40.00]Line 3 Chorus Starts\n"
        "[00:50.00]Line 4 Chorus Continues\n"
        "[01:00.00]Line 5 Outro\n";
    memset(&s_sample_lyrics, 0, sizeof(s_sample_lyrics));
    lrc_parse(sample, &s_sample_lyrics);
}

void setUp(void) {
    ui_init();
    setup_sample_lyrics();
}

void tearDown(void) {
}

// =============================================================================
// 1. format_time_ms Unit Tests (0ms, 65000ms, >1hr, null & boundary safety)
// =============================================================================

void test_format_time_zero_ms(void) {
    char buf[32] = {0};
    format_time_ms(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00:00", buf);
}

void test_format_time_under_one_minute(void) {
    char buf[32] = {0};
    format_time_ms(12000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00:12", buf);

    format_time_ms(59000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00:59", buf);
}

void test_format_time_65000_ms(void) {
    char buf[32] = {0};
    format_time_ms(65000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("01:05", buf);
}

void test_format_time_exact_one_hour(void) {
    char buf[32] = {0};
    format_time_ms(3600000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("01:00:00", buf);
}

void test_format_time_over_one_hour(void) {
    char buf[32] = {0};
    // 1 hour, 1 minute, 5 seconds = 3665000 ms
    format_time_ms(3665000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("01:01:05", buf);
}

void test_format_time_multiple_hours(void) {
    char buf[32] = {0};
    // 2 hours, 2 minutes, 5 seconds = 7325000 ms
    format_time_ms(7325000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("02:02:05", buf);
}

void test_format_time_null_and_zero_len(void) {
    char buf[32] = "untouched";
    // NULL destination buffer should return safely without crash
    format_time_ms(65000, NULL, sizeof(buf));

    // Zero out_len should not write to buffer
    format_time_ms(65000, buf, 0);
    TEST_ASSERT_EQUAL_STRING("untouched", buf);
}

void test_format_time_small_buffer_truncation(void) {
    char buf[4] = { 'X', 'X', 'X', 'X' };
    format_time_ms(65000, buf, sizeof(buf));
    // Should safely null terminate within 4 bytes
    TEST_ASSERT_EQUAL_UINT8('\0', buf[3]);
    TEST_ASSERT_EQUAL_UINT32(3, strlen(buf));
}

// =============================================================================
// 2. truncate_for_display Unit Tests (boundary cases, null safety)
// =============================================================================

void test_truncate_normal_string_fits(void) {
    char buf[32] = {0};
    truncate_for_display("Short Title", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Short Title", buf);
}

void test_truncate_exact_fit(void) {
    char buf[6] = {0}; // Holds 5 chars + null terminator
    truncate_for_display("12345", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("12345", buf);
    TEST_ASSERT_EQUAL_UINT32(5, strlen(buf));
}

void test_truncate_long_string_with_ellipsis(void) {
    char buf[32] = {0};
    truncate_for_display("This is an extremely long song title that exceeds screen width", buf, sizeof(buf));
    TEST_ASSERT_LESS_OR_EQUAL(31, strlen(buf));
    TEST_ASSERT_EQUAL_UINT32(31, strlen(buf));
    // Must end with "..."
    TEST_ASSERT_EQUAL_STRING("...", &buf[28]);
}

void test_truncate_boundary_cases(void) {
    char buf1[1] = { 'A' };
    truncate_for_display("Hello", buf1, sizeof(buf1));
    TEST_ASSERT_EQUAL_UINT8('\0', buf1[0]);

    char buf2[2] = { 'A', 'B' };
    truncate_for_display("Hello", buf2, sizeof(buf2));
    TEST_ASSERT_EQUAL_STRING("H", buf2);

    char buf3[3] = { 'A', 'B', 'C' };
    truncate_for_display("Hello", buf3, sizeof(buf3));
    TEST_ASSERT_EQUAL_STRING("He", buf3);

    char buf4[4] = { 'A', 'B', 'C', 'D' };
    truncate_for_display("Hello", buf4, sizeof(buf4));
    TEST_ASSERT_EQUAL_STRING("...", buf4);

    char buf5[5] = { 'A', 'B', 'C', 'D', 'E' };
    truncate_for_display("Hello", buf5, sizeof(buf5));
    TEST_ASSERT_EQUAL_STRING("H...", buf5);
}

void test_truncate_null_and_empty_safety(void) {
    char buf[32] = "Initial";
    truncate_for_display(NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);

    truncate_for_display("", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);

    // NULL destination
    truncate_for_display("Hello", NULL, 32);

    // Zero max_len
    char buf2[10] = "Safe";
    truncate_for_display("Hello", buf2, 0);
    TEST_ASSERT_EQUAL_STRING("Safe", buf2);
}

// =============================================================================
// 3. compute_lyric_window Unit Tests (beginning, middle, end, fallback)
// =============================================================================

void test_compute_lyric_window_empty_lyrics_fallback(void) {
    LyricsData empty;
    memset(&empty, 0, sizeof(empty));
    char window[5][128];
    int active_idx = 99;

    compute_lyric_window(&empty, 5000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(-1, active_idx);
    TEST_ASSERT_EQUAL_STRING("", window[0]);
    TEST_ASSERT_EQUAL_STRING("", window[1]);
    TEST_ASSERT_EQUAL_STRING("No lyrics available", window[2]);
    TEST_ASSERT_EQUAL_STRING("", window[3]);
    TEST_ASSERT_EQUAL_STRING("", window[4]);
}

void test_compute_lyric_window_null_lyrics_fallback(void) {
    char window[5][128];
    int active_idx = 99;

    compute_lyric_window(NULL, 1000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(-1, active_idx);
    TEST_ASSERT_EQUAL_STRING("No lyrics available", window[2]);
    TEST_ASSERT_EQUAL_STRING("", window[0]);
    TEST_ASSERT_EQUAL_STRING("", window[1]);
    TEST_ASSERT_EQUAL_STRING("", window[3]);
    TEST_ASSERT_EQUAL_STRING("", window[4]);
}

void test_compute_lyric_window_unloaded_lyrics_fallback(void) {
    LyricsData unloaded;
    memset(&unloaded, 0, sizeof(unloaded));
    unloaded.count = 5;
    unloaded.loaded = false; // Not yet parsed/loaded
    char window[5][128];
    int active_idx = 0;

    compute_lyric_window(&unloaded, 20000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(-1, active_idx);
    TEST_ASSERT_EQUAL_STRING("No lyrics available", window[2]);
}

void test_compute_lyric_window_beginning_before_first_line(void) {
    char window[5][128];
    int active_idx = 0;

    // Line 0 is at 10000ms. At 5000ms we are in the intro.
    compute_lyric_window(&s_sample_lyrics, 5000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(-1, active_idx);
    TEST_ASSERT_EQUAL_STRING("", window[0]);
    TEST_ASSERT_EQUAL_STRING("", window[1]);
    TEST_ASSERT_EQUAL_STRING("", window[2]);
    TEST_ASSERT_EQUAL_STRING("Line 0 Intro Ends", window[3]);
    TEST_ASSERT_EQUAL_STRING("Line 1 Verse 1", window[4]);
}

void test_compute_lyric_window_beginning_at_first_line(void) {
    char window[5][128];
    int active_idx = -1;

    // Line 0 is active at 10000ms
    compute_lyric_window(&s_sample_lyrics, 10000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(0, active_idx);
    TEST_ASSERT_EQUAL_STRING("", window[0]);
    TEST_ASSERT_EQUAL_STRING("", window[1]);
    TEST_ASSERT_EQUAL_STRING("Line 0 Intro Ends", window[2]);
    TEST_ASSERT_EQUAL_STRING("Line 1 Verse 1", window[3]);
    TEST_ASSERT_EQUAL_STRING("Line 2 Verse 2", window[4]);
}

void test_compute_lyric_window_beginning_at_second_line(void) {
    char window[5][128];
    int active_idx = -1;

    // Line 1 is active at 20000ms
    compute_lyric_window(&s_sample_lyrics, 20000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(1, active_idx);
    TEST_ASSERT_EQUAL_STRING("", window[0]);
    TEST_ASSERT_EQUAL_STRING("Line 0 Intro Ends", window[1]);
    TEST_ASSERT_EQUAL_STRING("Line 1 Verse 1", window[2]);
    TEST_ASSERT_EQUAL_STRING("Line 2 Verse 2", window[3]);
    TEST_ASSERT_EQUAL_STRING("Line 3 Chorus Starts", window[4]);
}

void test_compute_lyric_window_middle_of_song(void) {
    char window[5][128];
    int active_idx = -1;

    // Line 2 (at 30000ms) active during 35000ms
    compute_lyric_window(&s_sample_lyrics, 35000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(2, active_idx);
    TEST_ASSERT_EQUAL_STRING("Line 0 Intro Ends", window[0]);
    TEST_ASSERT_EQUAL_STRING("Line 1 Verse 1", window[1]);
    TEST_ASSERT_EQUAL_STRING("Line 2 Verse 2", window[2]);
    TEST_ASSERT_EQUAL_STRING("Line 3 Chorus Starts", window[3]);
    TEST_ASSERT_EQUAL_STRING("Line 4 Chorus Continues", window[4]);
}

void test_compute_lyric_window_end_second_to_last_line(void) {
    char window[5][128];
    int active_idx = -1;

    // Line 4 (at 50000ms) active during 55000ms
    compute_lyric_window(&s_sample_lyrics, 55000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(4, active_idx);
    TEST_ASSERT_EQUAL_STRING("Line 2 Verse 2", window[0]);
    TEST_ASSERT_EQUAL_STRING("Line 3 Chorus Starts", window[1]);
    TEST_ASSERT_EQUAL_STRING("Line 4 Chorus Continues", window[2]);
    TEST_ASSERT_EQUAL_STRING("Line 5 Outro", window[3]);
    TEST_ASSERT_EQUAL_STRING("", window[4]);
}

void test_compute_lyric_window_end_last_line(void) {
    char window[5][128];
    int active_idx = -1;

    // Line 5 (at 60000ms) active at 60000ms and beyond
    compute_lyric_window(&s_sample_lyrics, 60000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(5, active_idx);
    TEST_ASSERT_EQUAL_STRING("Line 3 Chorus Starts", window[0]);
    TEST_ASSERT_EQUAL_STRING("Line 4 Chorus Continues", window[1]);
    TEST_ASSERT_EQUAL_STRING("Line 5 Outro", window[2]);
    TEST_ASSERT_EQUAL_STRING("", window[3]);
    TEST_ASSERT_EQUAL_STRING("", window[4]);

    // Way past the end
    compute_lyric_window(&s_sample_lyrics, 120000, window, &active_idx);
    TEST_ASSERT_EQUAL_INT(5, active_idx);
    TEST_ASSERT_EQUAL_STRING("Line 5 Outro", window[2]);
    TEST_ASSERT_EQUAL_STRING("", window[3]);
    TEST_ASSERT_EQUAL_STRING("", window[4]);
}

void test_compute_lyric_window_null_safety(void) {
    int active_idx = -1;
    // NULL window should return safely without crash
    compute_lyric_window(&s_sample_lyrics, 20000, NULL, &active_idx);

    // NULL active_idx pointer should work normally
    char window[5][128];
    compute_lyric_window(&s_sample_lyrics, 20000, window, NULL);
    TEST_ASSERT_EQUAL_STRING("Line 1 Verse 1", window[2]);
}

// =============================================================================
// 4. UI State Tracking Unit Tests
// =============================================================================

void test_ui_init_defaults(void) {
    const UIState *state = ui_get_state();
    TEST_ASSERT_NOT_NULL(state);
    TEST_ASSERT_EQUAL_INT(SOURCE_NONE, state->source);
    TEST_ASSERT_EQUAL_STRING("", state->title);
    TEST_ASSERT_EQUAL_STRING("", state->artist);
    TEST_ASSERT_EQUAL_UINT32(0, state->current_ms);
    TEST_ASSERT_EQUAL_UINT32(0, state->total_ms);
    TEST_ASSERT_FALSE(state->wifi_connected);
    TEST_ASSERT_EQUAL_INT(-1, state->active_lyric_index);
    TEST_ASSERT_EQUAL_STRING("No lyrics available", state->lyric_window[2]);
}

void test_ui_update_track_bluetooth(void) {
    ui_update_track("Levitating", "Dua Lipa", SOURCE_BLUETOOTH);
    const UIState *state = ui_get_state();
    TEST_ASSERT_EQUAL_STRING("Levitating", state->title);
    TEST_ASSERT_EQUAL_STRING("Dua Lipa", state->artist);
    TEST_ASSERT_EQUAL_INT(SOURCE_BLUETOOTH, state->source);
}

void test_ui_update_track_airplay(void) {
    ui_update_track("Blinding Lights", "The Weeknd", SOURCE_AIRPLAY);
    const UIState *state = ui_get_state();
    TEST_ASSERT_EQUAL_STRING("Blinding Lights", state->title);
    TEST_ASSERT_EQUAL_STRING("The Weeknd", state->artist);
    TEST_ASSERT_EQUAL_INT(SOURCE_AIRPLAY, state->source);
}

void test_ui_update_track_null_safety(void) {
    ui_update_track(NULL, NULL, SOURCE_NONE);
    const UIState *state = ui_get_state();
    TEST_ASSERT_EQUAL_STRING("", state->title);
    TEST_ASSERT_EQUAL_STRING("", state->artist);
    TEST_ASSERT_EQUAL_INT(SOURCE_NONE, state->source);
}

void test_ui_update_progress_and_lyric_sync(void) {
    ui_update_lyrics(&s_sample_lyrics);
    ui_update_progress(35000, 180000);

    const UIState *state = ui_get_state();
    TEST_ASSERT_EQUAL_UINT32(35000, state->current_ms);
    TEST_ASSERT_EQUAL_UINT32(180000, state->total_ms);
    TEST_ASSERT_EQUAL_INT(2, state->active_lyric_index);
    TEST_ASSERT_EQUAL_STRING("Line 2 Verse 2", state->lyric_window[2]);
    TEST_ASSERT_EQUAL_STRING("Line 1 Verse 1", state->lyric_window[1]);
    TEST_ASSERT_EQUAL_STRING("Line 3 Chorus Starts", state->lyric_window[3]);
}

void test_ui_set_wifi_status(void) {
    ui_set_wifi_status(true);
    TEST_ASSERT_TRUE(ui_get_state()->wifi_connected);

    ui_set_wifi_status(false);
    TEST_ASSERT_FALSE(ui_get_state()->wifi_connected);
}

void test_ui_update_lyrics_clear(void) {
    ui_update_lyrics(&s_sample_lyrics);
    ui_update_progress(30000, 100000);
    TEST_ASSERT_EQUAL_STRING("Line 2 Verse 2", ui_get_state()->lyric_window[2]);

    // Clearing lyrics should restore fallback message
    ui_update_lyrics(NULL);
    const UIState *state = ui_get_state();
    TEST_ASSERT_EQUAL_INT(-1, state->active_lyric_index);
    TEST_ASSERT_EQUAL_STRING("No lyrics available", state->lyric_window[2]);
    TEST_ASSERT_EQUAL_STRING("", state->lyric_window[0]);
    TEST_ASSERT_EQUAL_STRING("", state->lyric_window[1]);
    TEST_ASSERT_EQUAL_STRING("", state->lyric_window[3]);
    TEST_ASSERT_EQUAL_STRING("", state->lyric_window[4]);
}

void test_ui_task_tick(void) {
    // Should run smoothly without errors or side effects in native mode
    ui_task_tick(30);
    ui_task_tick(100);
    TEST_ASSERT_NOT_NULL(ui_get_state());
}

// =============================================================================
// Test Runner
// =============================================================================

int main(void) {
    UNITY_BEGIN();

    // 1. format_time_ms
    RUN_TEST(test_format_time_zero_ms);
    RUN_TEST(test_format_time_under_one_minute);
    RUN_TEST(test_format_time_65000_ms);
    RUN_TEST(test_format_time_exact_one_hour);
    RUN_TEST(test_format_time_over_one_hour);
    RUN_TEST(test_format_time_multiple_hours);
    RUN_TEST(test_format_time_null_and_zero_len);
    RUN_TEST(test_format_time_small_buffer_truncation);

    // 2. truncate_for_display
    RUN_TEST(test_truncate_normal_string_fits);
    RUN_TEST(test_truncate_exact_fit);
    RUN_TEST(test_truncate_long_string_with_ellipsis);
    RUN_TEST(test_truncate_boundary_cases);
    RUN_TEST(test_truncate_null_and_empty_safety);

    // 3. compute_lyric_window
    RUN_TEST(test_compute_lyric_window_empty_lyrics_fallback);
    RUN_TEST(test_compute_lyric_window_null_lyrics_fallback);
    RUN_TEST(test_compute_lyric_window_unloaded_lyrics_fallback);
    RUN_TEST(test_compute_lyric_window_beginning_before_first_line);
    RUN_TEST(test_compute_lyric_window_beginning_at_first_line);
    RUN_TEST(test_compute_lyric_window_beginning_at_second_line);
    RUN_TEST(test_compute_lyric_window_middle_of_song);
    RUN_TEST(test_compute_lyric_window_end_second_to_last_line);
    RUN_TEST(test_compute_lyric_window_end_last_line);
    RUN_TEST(test_compute_lyric_window_null_safety);

    // 4. UI State Tracking
    RUN_TEST(test_ui_init_defaults);
    RUN_TEST(test_ui_update_track_bluetooth);
    RUN_TEST(test_ui_update_track_airplay);
    RUN_TEST(test_ui_update_track_null_safety);
    RUN_TEST(test_ui_update_progress_and_lyric_sync);
    RUN_TEST(test_ui_set_wifi_status);
    RUN_TEST(test_ui_update_lyrics_clear);
    RUN_TEST(test_ui_task_tick);

    return UNITY_END();
}
