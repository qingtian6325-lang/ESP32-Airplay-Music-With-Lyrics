#ifndef STATE_ARBITER_H
#define STATE_ARBITER_H

#include <stdint.h>
#include <stdbool.h>
#include "ui_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief System audio playback states.
 */
typedef enum SystemAudioState {
    STATE_IDLE = 0,
    STATE_BT_PLAYING = 1,
    STATE_AIRPLAY_PLAYING = 2
} SystemAudioState;

// Alias for plan compatibility
typedef SystemAudioState AudioSource;

/**
 * @brief Events that trigger audio source arbitration.
 */
typedef enum AudioEvent {
    EVENT_NONE = 0,
    EVENT_BT_CONNECT,
    EVENT_BT_START,
    EVENT_BT_STOP,
    EVENT_BT_DISCONNECT,
    EVENT_AIRPLAY_START,
    EVENT_AIRPLAY_STOP
} AudioEvent;

/**
 * @brief Pure state transition function for audio source arbitration.
 *
 * Rules:
 * - AirPlay has higher priority than Bluetooth.
 * - If Bluetooth is playing and AirPlay starts, AirPlay preempts Bluetooth.
 * - If AirPlay is playing, Bluetooth start events are ignored.
 * - Stops and disconnects return the state to IDLE.
 *
 * @param current_state Current SystemAudioState.
 * @param event AudioEvent to process.
 * @return New SystemAudioState after transition.
 */
SystemAudioState arbitrate_audio_event(SystemAudioState current_state, AudioEvent event);

/**
 * @brief Get human-readable string representation of a SystemAudioState.
 *
 * @param state SystemAudioState value.
 * @return String description ("IDLE", "BT_PLAYING", "AIRPLAY_PLAYING", or "UNKNOWN").
 */
const char* audio_state_to_string(SystemAudioState state);

/**
 * @brief Get human-readable string representation of an AudioEvent.
 *
 * @param event AudioEvent value.
 * @return String description.
 */
const char* audio_event_to_string(AudioEvent event);

/**
 * @brief Convert SystemAudioState to UI AudioSourceType.
 *
 * @param state SystemAudioState value.
 * @return Corresponding AudioSourceType (SOURCE_NONE, SOURCE_BLUETOOTH, SOURCE_AIRPLAY).
 */
AudioSourceType audio_state_to_source_type(SystemAudioState state);

#ifdef __cplusplus
}

// C++ overload supporting AudioSourceType directly
inline AudioSourceType arbitrate_audio_event(AudioSourceType current_source, AudioEvent event) {
    return audio_state_to_source_type(arbitrate_audio_event((SystemAudioState)current_source, event));
}
#endif

#endif // STATE_ARBITER_H
