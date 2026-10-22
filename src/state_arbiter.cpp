#include "state_arbiter.h"

SystemAudioState arbitrate_audio_event(SystemAudioState current_state, AudioEvent event) {
    switch (current_state) {
        case STATE_IDLE:
            switch (event) {
                case EVENT_BT_START:
                    return STATE_BT_PLAYING;
                case EVENT_AIRPLAY_START:
                    return STATE_AIRPLAY_PLAYING;
                case EVENT_BT_CONNECT:
                case EVENT_BT_STOP:
                case EVENT_BT_DISCONNECT:
                case EVENT_AIRPLAY_STOP:
                case EVENT_NONE:
                default:
                    return STATE_IDLE;
            }

        case STATE_BT_PLAYING:
            switch (event) {
                case EVENT_AIRPLAY_START:
                    // AirPlay preempts Bluetooth playback immediately
                    return STATE_AIRPLAY_PLAYING;
                case EVENT_BT_STOP:
                case EVENT_BT_DISCONNECT:
                    return STATE_IDLE;
                case EVENT_BT_START:
                case EVENT_BT_CONNECT:
                case EVENT_AIRPLAY_STOP:
                case EVENT_NONE:
                default:
                    return STATE_BT_PLAYING;
            }

        case STATE_AIRPLAY_PLAYING:
            switch (event) {
                case EVENT_AIRPLAY_STOP:
                    return STATE_IDLE;
                case EVENT_BT_START:
                    // AirPlay has higher priority; Bluetooth playback requests are ignored
                    return STATE_AIRPLAY_PLAYING;
                case EVENT_BT_CONNECT:
                case EVENT_BT_STOP:
                case EVENT_BT_DISCONNECT:
                case EVENT_AIRPLAY_START:
                case EVENT_NONE:
                default:
                    return STATE_AIRPLAY_PLAYING;
            }

        default:
            // Recover from unknown / corrupted state to IDLE or handle valid start event
            if (event == EVENT_BT_START) {
                return STATE_BT_PLAYING;
            } else if (event == EVENT_AIRPLAY_START) {
                return STATE_AIRPLAY_PLAYING;
            }
            return STATE_IDLE;
    }
}

const char* audio_state_to_string(SystemAudioState state) {
    switch (state) {
        case STATE_IDLE:
            return "IDLE";
        case STATE_BT_PLAYING:
            return "BT_PLAYING";
        case STATE_AIRPLAY_PLAYING:
            return "AIRPLAY_PLAYING";
        default:
            return "UNKNOWN";
    }
}

const char* audio_event_to_string(AudioEvent event) {
    switch (event) {
        case EVENT_NONE:
            return "EVENT_NONE";
        case EVENT_BT_CONNECT:
            return "EVENT_BT_CONNECT";
        case EVENT_BT_START:
            return "EVENT_BT_START";
        case EVENT_BT_STOP:
            return "EVENT_BT_STOP";
        case EVENT_BT_DISCONNECT:
            return "EVENT_BT_DISCONNECT";
        case EVENT_AIRPLAY_START:
            return "EVENT_AIRPLAY_START";
        case EVENT_AIRPLAY_STOP:
            return "EVENT_AIRPLAY_STOP";
        default:
            return "EVENT_UNKNOWN";
    }
}

AudioSourceType audio_state_to_source_type(SystemAudioState state) {
    switch (state) {
        case STATE_BT_PLAYING:
            return SOURCE_BLUETOOTH;
        case STATE_AIRPLAY_PLAYING:
            return SOURCE_AIRPLAY;
        case STATE_IDLE:
        default:
            return SOURCE_NONE;
    }
}
