#include <unity.h>
#include <string.h>
#include "state_arbiter.h"
#include "ui_display.h"

void setUp(void) {
}

void tearDown(void) {
}

// =============================================================================
// Idle State Transitions
// =============================================================================

void test_idle_to_bt_playing(void) {
    SystemAudioState state = STATE_IDLE;
    state = arbitrate_audio_event(state, EVENT_BT_START);
    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, state);
}

void test_idle_to_airplay_playing(void) {
    SystemAudioState state = STATE_IDLE;
    state = arbitrate_audio_event(state, EVENT_AIRPLAY_START);
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, state);
}

void test_idle_ignores_bt_connect_and_stops(void) {
    SystemAudioState state = STATE_IDLE;

    TEST_ASSERT_EQUAL(STATE_IDLE, arbitrate_audio_event(state, EVENT_BT_CONNECT));
    TEST_ASSERT_EQUAL(STATE_IDLE, arbitrate_audio_event(state, EVENT_BT_STOP));
    TEST_ASSERT_EQUAL(STATE_IDLE, arbitrate_audio_event(state, EVENT_BT_DISCONNECT));
    TEST_ASSERT_EQUAL(STATE_IDLE, arbitrate_audio_event(state, EVENT_AIRPLAY_STOP));
    TEST_ASSERT_EQUAL(STATE_IDLE, arbitrate_audio_event(state, EVENT_NONE));
}

// =============================================================================
// Bluetooth Playback Transitions
// =============================================================================

void test_bt_playing_to_idle_on_stop(void) {
    SystemAudioState state = STATE_BT_PLAYING;
    state = arbitrate_audio_event(state, EVENT_BT_STOP);
    TEST_ASSERT_EQUAL(STATE_IDLE, state);
}

void test_bt_playing_to_idle_on_disconnect(void) {
    SystemAudioState state = STATE_BT_PLAYING;
    state = arbitrate_audio_event(state, EVENT_BT_DISCONNECT);
    TEST_ASSERT_EQUAL(STATE_IDLE, state);
}

void test_bt_playing_ignores_redundant_events(void) {
    SystemAudioState state = STATE_BT_PLAYING;

    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, arbitrate_audio_event(state, EVENT_BT_START));
    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, arbitrate_audio_event(state, EVENT_BT_CONNECT));
    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, arbitrate_audio_event(state, EVENT_AIRPLAY_STOP));
    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, arbitrate_audio_event(state, EVENT_NONE));
}

// =============================================================================
// AirPlay Preemption over Bluetooth (Core Arbiter Requirement)
// =============================================================================

void test_airplay_preempts_bluetooth(void) {
    // Plan step requirement: AudioSource active = SOURCE_BLUETOOTH; ...
    AudioSource active = (AudioSource)SOURCE_BLUETOOTH;
    active = arbitrate_audio_event(active, EVENT_AIRPLAY_START);
    TEST_ASSERT_EQUAL(SOURCE_AIRPLAY, active);
}

void test_airplay_preempts_bt_playing_enum(void) {
    SystemAudioState state = STATE_BT_PLAYING;
    state = arbitrate_audio_event(state, EVENT_AIRPLAY_START);
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, state);
}

// =============================================================================
// AirPlay Playback Transitions & Bluetooth Rejection
// =============================================================================

void test_airplay_playing_to_idle_on_stop(void) {
    SystemAudioState state = STATE_AIRPLAY_PLAYING;
    state = arbitrate_audio_event(state, EVENT_AIRPLAY_STOP);
    TEST_ASSERT_EQUAL(STATE_IDLE, state);
}

void test_airplay_rejects_bt_start_while_playing(void) {
    SystemAudioState state = STATE_AIRPLAY_PLAYING;
    // BT start must NOT preempt active AirPlay stream
    state = arbitrate_audio_event(state, EVENT_BT_START);
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, state);
}

void test_airplay_ignores_bt_events_and_redundant_start(void) {
    SystemAudioState state = STATE_AIRPLAY_PLAYING;

    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, arbitrate_audio_event(state, EVENT_BT_CONNECT));
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, arbitrate_audio_event(state, EVENT_BT_STOP));
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, arbitrate_audio_event(state, EVENT_BT_DISCONNECT));
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, arbitrate_audio_event(state, EVENT_AIRPLAY_START));
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, arbitrate_audio_event(state, EVENT_NONE));
}

// =============================================================================
// Multi-step Full Lifecycle Scenarios
// =============================================================================

void test_full_lifecycle_bluetooth_session(void) {
    SystemAudioState state = STATE_IDLE;

    // Connect BT
    state = arbitrate_audio_event(state, EVENT_BT_CONNECT);
    TEST_ASSERT_EQUAL(STATE_IDLE, state);

    // Start playing
    state = arbitrate_audio_event(state, EVENT_BT_START);
    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, state);

    // Stop playing
    state = arbitrate_audio_event(state, EVENT_BT_STOP);
    TEST_ASSERT_EQUAL(STATE_IDLE, state);

    // Disconnect
    state = arbitrate_audio_event(state, EVENT_BT_DISCONNECT);
    TEST_ASSERT_EQUAL(STATE_IDLE, state);
}

void test_full_lifecycle_preemption_and_recovery(void) {
    SystemAudioState state = STATE_IDLE;

    // BT starts playing
    state = arbitrate_audio_event(state, EVENT_BT_START);
    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, state);

    // AirPlay interrupts and preempts BT
    state = arbitrate_audio_event(state, EVENT_AIRPLAY_START);
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, state);

    // While AirPlay plays, BT attempts to start again (must be ignored)
    state = arbitrate_audio_event(state, EVENT_BT_START);
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, state);

    // AirPlay session finishes
    state = arbitrate_audio_event(state, EVENT_AIRPLAY_STOP);
    TEST_ASSERT_EQUAL(STATE_IDLE, state);

    // BT can resume playing now
    state = arbitrate_audio_event(state, EVENT_BT_START);
    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, state);
}

// =============================================================================
// Unexpected and Boundary Event Resilience
// =============================================================================

void test_unexpected_events_and_invalid_state(void) {
    // Unknown event on known states should preserve state
    AudioEvent invalid_event = (AudioEvent)999;
    TEST_ASSERT_EQUAL(STATE_IDLE, arbitrate_audio_event(STATE_IDLE, invalid_event));
    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, arbitrate_audio_event(STATE_BT_PLAYING, invalid_event));
    TEST_ASSERT_EQUAL(STATE_AIRPLAY_PLAYING, arbitrate_audio_event(STATE_AIRPLAY_PLAYING, invalid_event));

    // Completely invalid current_state should recover safely to STATE_IDLE
    SystemAudioState invalid_state = (SystemAudioState)999;
    TEST_ASSERT_EQUAL(STATE_IDLE, arbitrate_audio_event(invalid_state, EVENT_NONE));
    TEST_ASSERT_EQUAL(STATE_BT_PLAYING, arbitrate_audio_event(invalid_state, EVENT_BT_START));
}

// =============================================================================
// String Conversion & Type Mapping Helpers
// =============================================================================

void test_audio_state_to_string(void) {
    TEST_ASSERT_EQUAL_STRING("IDLE", audio_state_to_string(STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("BT_PLAYING", audio_state_to_string(STATE_BT_PLAYING));
    TEST_ASSERT_EQUAL_STRING("AIRPLAY_PLAYING", audio_state_to_string(STATE_AIRPLAY_PLAYING));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", audio_state_to_string((SystemAudioState)123));
}

void test_audio_event_to_string(void) {
    TEST_ASSERT_EQUAL_STRING("EVENT_NONE", audio_event_to_string(EVENT_NONE));
    TEST_ASSERT_EQUAL_STRING("EVENT_BT_CONNECT", audio_event_to_string(EVENT_BT_CONNECT));
    TEST_ASSERT_EQUAL_STRING("EVENT_BT_START", audio_event_to_string(EVENT_BT_START));
    TEST_ASSERT_EQUAL_STRING("EVENT_BT_STOP", audio_event_to_string(EVENT_BT_STOP));
    TEST_ASSERT_EQUAL_STRING("EVENT_BT_DISCONNECT", audio_event_to_string(EVENT_BT_DISCONNECT));
    TEST_ASSERT_EQUAL_STRING("EVENT_AIRPLAY_START", audio_event_to_string(EVENT_AIRPLAY_START));
    TEST_ASSERT_EQUAL_STRING("EVENT_AIRPLAY_STOP", audio_event_to_string(EVENT_AIRPLAY_STOP));
    TEST_ASSERT_EQUAL_STRING("EVENT_UNKNOWN", audio_event_to_string((AudioEvent)999));
}

void test_audio_state_to_source_type(void) {
    TEST_ASSERT_EQUAL(SOURCE_NONE, audio_state_to_source_type(STATE_IDLE));
    TEST_ASSERT_EQUAL(SOURCE_BLUETOOTH, audio_state_to_source_type(STATE_BT_PLAYING));
    TEST_ASSERT_EQUAL(SOURCE_AIRPLAY, audio_state_to_source_type(STATE_AIRPLAY_PLAYING));
    TEST_ASSERT_EQUAL(SOURCE_NONE, audio_state_to_source_type((SystemAudioState)999));
}

// =============================================================================
// Test Runner
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Idle
    RUN_TEST(test_idle_to_bt_playing);
    RUN_TEST(test_idle_to_airplay_playing);
    RUN_TEST(test_idle_ignores_bt_connect_and_stops);

    // Bluetooth
    RUN_TEST(test_bt_playing_to_idle_on_stop);
    RUN_TEST(test_bt_playing_to_idle_on_disconnect);
    RUN_TEST(test_bt_playing_ignores_redundant_events);

    // Preemption
    RUN_TEST(test_airplay_preempts_bluetooth);
    RUN_TEST(test_airplay_preempts_bt_playing_enum);

    // AirPlay
    RUN_TEST(test_airplay_playing_to_idle_on_stop);
    RUN_TEST(test_airplay_rejects_bt_start_while_playing);
    RUN_TEST(test_airplay_ignores_bt_events_and_redundant_start);

    // Lifecycle
    RUN_TEST(test_full_lifecycle_bluetooth_session);
    RUN_TEST(test_full_lifecycle_preemption_and_recovery);

    // Robustness & Strings
    RUN_TEST(test_unexpected_events_and_invalid_state);
    RUN_TEST(test_audio_state_to_string);
    RUN_TEST(test_audio_event_to_string);
    RUN_TEST(test_audio_state_to_source_type);

    return UNITY_END();
}
