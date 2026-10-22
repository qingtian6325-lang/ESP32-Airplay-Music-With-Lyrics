# ESP32 AirPlay & Bluetooth Lyrics Speaker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a complete firmware and DIY hardware specification for an ESP32-WROVER-E desktop speaker that streams audio over AirPlay 1 (RAOP) and Classic Bluetooth A2DP Sink while displaying synchronized scrolling lyrics on an SPI TFT display.

**Architecture:** The system runs on ESP32-WROVER-E (Dual-Core 240MHz with 8MB PSRAM). Core 0 handles network (Wi-Fi), Bluetooth A2DP/AVRCP, AirPlay RTSP/ALAC audio streaming, and pushes decoded PCM into an I2S DMA RingBuffer feeding the MAX98357A. Core 1 runs the LVGL 8.x UI task (rendering 5-line scrolling lyrics to the ST7789/ILI9341 screen) and executes async HTTP requests to the LRCLIB open lyrics database upon track changes.

**Tech Stack:** PlatformIO, Arduino-ESP32 / ESP-IDF framework, `ESP32-A2DP`, `ArduinoJson`, `TFT_eSPI` / `lvgl 8.x`, HTTPClient, FreeRTOS.

**Spec:** [docs/superpowers/specs/2026-10-22-esp32-airplay-bluetooth-lyrics-speaker-design.md](file:///c:/Users/hp/Documents/antigravity/vibrant-hubble/docs/superpowers/specs/2026-10-22-esp32-airplay-bluetooth-lyrics-speaker-design.md)

## Global Constraints

- **MCU Hardware**: ESP32-WROVER-E (CH340) with 8MB PSRAM and 16MB Flash.
- **Forbidden Pins**: GPIO 16 and GPIO 17 are strictly reserved for internal PSRAM; do not assign to external peripherals.
- **I2S Pinout**: BCLK = GPIO 25, LRC = GPIO 26, DIN = GPIO 27 (feeding MAX98357A).
- **SPI Pinout**: CS = GPIO 5, DC = GPIO 2, RST = GPIO 4, SCK = GPIO 18, MOSI = GPIO 23, BLK = GPIO 21 (feeding ST7789/ILI9341).
- **Audio Output Format**: 44.1kHz sample rate, 16-bit depth, stereo or mixed mono.
- **Display Resolution**: 320×240 landscape orientation.

## Review Focus

1. **PSRAM Allocation Failure**: If PSRAM is not initialized properly at boot, large audio ringbuffers and LVGL framebuffers will cause heap panic. Verified via `ESP.getPsramSize() > 0`.
2. **Missing or Delayed Lyrics**: When a song is instrumental or not in LRCLIB, the UI must gracefully fall back to "Title + Artist + Fake Spectrum" rather than freezing or displaying empty screens.
3. **Audio Glitches during Wi-Fi HTTP Fetch**: Fetching lyrics over HTTP while streaming Bluetooth or AirPlay could cause DMA buffer underruns if run on the audio core. Verified by pinning HTTP and LVGL tasks to Core 1 with lower priority than the I2S audio task on Core 0.
4. **Bluetooth to AirPlay Transition Conflict**: Both sources attempting to drive I2S simultaneously. The audio arbiter must immediately pause/disconnect Bluetooth audio when an AirPlay RTSP session begins.
5. **Timestamp Drift**: AVRCP playback position sync vs local millis() timer drift over long tracks. Tested with resync logic upon AVRCP position updates.

---

### Task 1: Project Scaffolding & Pin Configuration

**Files:**
- Create: `platformio.ini`
- Create: `include/config.h`
- Create: `src/main.cpp`
- Test: `test/test_config/test_config.cpp`

**Interfaces:**
- Consumes: None
- Produces: Board pin definitions (`I2S_BCLK_PIN`, `I2S_LRC_PIN`, `I2S_DIN_PIN`, `TFT_CS_PIN`, etc.) and verified PSRAM configuration.

- [ ] **Step 1: Write native host unit test to verify pin safety and constraints**

```cpp
#include <unity.h>
#include "config.h"

void test_forbidden_pins_not_used() {
    TEST_ASSERT_NOT_EQUAL(16, I2S_BCLK_PIN);
    TEST_ASSERT_NOT_EQUAL(16, I2S_LRC_PIN);
    TEST_ASSERT_NOT_EQUAL(16, I2S_DIN_PIN);
    TEST_ASSERT_NOT_EQUAL(17, I2S_BCLK_PIN);
    TEST_ASSERT_NOT_EQUAL(17, I2S_LRC_PIN);
    TEST_ASSERT_NOT_EQUAL(17, I2S_DIN_PIN);
}
```

- [ ] **Step 2: Run test to verify it fails without config.h**

Run: `pio test -e native`
Expected: FAIL with "config.h not found"

- [ ] **Step 3: Implement `platformio.ini`, `include/config.h`, and `src/main.cpp`**

Configure `esp32dev` environment with `board_build.partitions = huge_app.csv`, `-DBOARD_HAS_PSRAM`, `-mfix-esp32-psram-cache-issue`, and define the safe pins in `include/config.h`.

- [ ] **Step 4: Run unit test to verify pin configuration passes**

Run: `pio test -e native`
Expected: PASS

- [ ] **Step 5: Commit scaffolding**

```bash
git add platformio.ini include/config.h src/main.cpp test/test_config/test_config.cpp
git commit -m "chore: setup PlatformIO project for ESP32-WROVER-E with pin configuration"
```

---

### Task 2: I2S Audio Pipeline & MAX98357A Output Driver

**Files:**
- Create: `include/audio_i2s.h`
- Create: `src/audio_i2s.cpp`
- Test: `test/test_audio/test_audio.cpp`

**Interfaces:**
- Consumes: `config.h` (`I2S_BCLK_PIN`, `I2S_LRC_PIN`, `I2S_DIN_PIN`)
- Produces: `void audio_i2s_init()`, `size_t audio_i2s_write(const uint8_t *data, size_t len)`, `void audio_i2s_set_volume(uint8_t vol_percent)`

- [ ] **Step 1: Write test for audio buffer handling and volume scaling**

```cpp
#include <unity.h>
#include "audio_i2s.h"

void test_volume_scaling_zero() {
    int16_t sample = 1000;
    int16_t scaled = audio_apply_volume(sample, 0);
    TEST_ASSERT_EQUAL_INT16(0, scaled);
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `pio test -e native`
Expected: FAIL with undefined `audio_apply_volume`

- [ ] **Step 3: Implement `audio_apply_volume`, `audio_i2s_init`, and `audio_i2s_write`**

Configure ESP-IDF `i2s_driver_install` for standard I2S format, 44100Hz, 16-bit, DMA buffer size 512 samples × 4 buffers.

- [ ] **Step 4: Run test to verify audio processing passes**

Run: `pio test -e native`
Expected: PASS

- [ ] **Step 5: Commit audio I2S driver**

```bash
git add include/audio_i2s.h src/audio_i2s.cpp test/test_audio/test_audio.cpp
git commit -m "feat: implement I2S audio pipeline for MAX98357A amplifier"
```

---

### Task 3: Bluetooth A2DP Sink & AVRCP Metadata Handler

**Files:**
- Create: `include/bt_sink.h`
- Create: `src/bt_sink.cpp`
- Test: `test/test_bt_metadata/test_bt_metadata.cpp`

**Interfaces:**
- Consumes: `audio_i2s_write` from `audio_i2s.h`
- Produces: `void bt_sink_init(const char *device_name)`, `void register_track_change_callback(TrackChangeCallback cb)`

- [ ] **Step 1: Write unit test for AVRCP metadata parser and sanitization**

```cpp
#include <unity.h>
#include "bt_sink.h"

void test_metadata_extraction() {
    TrackInfo track;
    parse_avrcp_metadata("Shape of You", "Ed Sheeran", &track);
    TEST_ASSERT_EQUAL_STRING("Shape of You", track.title);
    TEST_ASSERT_EQUAL_STRING("Ed Sheeran", track.artist);
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `pio test -e native`
Expected: FAIL

- [ ] **Step 3: Implement `bt_sink.cpp` using ESP32-A2DP library**

Register A2DP audio data callback forwarding decoded PCM to `audio_i2s_write`, and AVRCP metadata callback forwarding track changes to registered listeners.

- [ ] **Step 4: Run test to verify metadata parsing passes**

Run: `pio test -e native`
Expected: PASS

- [ ] **Step 5: Commit Bluetooth sink**

```bash
git add include/bt_sink.h src/bt_sink.cpp test/test_bt_metadata/test_bt_metadata.cpp
git commit -m "feat: add Bluetooth A2DP sink and AVRCP metadata handler"
```

---

### Task 4: AirPlay 1 (RAOP) Stream & Metadata Receiver

**Files:**
- Create: `include/airplay_receiver.h`
- Create: `src/airplay_receiver.cpp`
- Test: `test/test_airplay/test_airplay.cpp`

**Interfaces:**
- Consumes: `audio_i2s_write`, Wi-Fi connection
- Produces: `void airplay_receiver_init(const char *service_name)`, `bool airplay_is_active()`

- [ ] **Step 1: Write unit test for DACP / DAAP metadata tag decoding**

```cpp
#include <unity.h>
#include "airplay_receiver.h"

void test_daap_minm_parsing() {
    const uint8_t raw_daap[] = { 'm','i','n','m', 0, 0, 0, 4, 'S','o','n','g' };
    char title[32] = {0};
    parse_daap_tag(raw_daap, sizeof(raw_daap), "minm", title, sizeof(title));
    TEST_ASSERT_EQUAL_STRING("Song", title);
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `pio test -e native`
Expected: FAIL

- [ ] **Step 3: Implement RAOP RTSP and ALAC audio receiver**

Publish mDNS service `_raop._tcp` and `_airplay._tcp`. Handle RTSP `ANNOUNCE`, `SETUP`, `RECORD`, `TEARDOWN`, and route decrypted ALAC frames to I2S.

- [ ] **Step 4: Run test to verify tag parser passes**

Run: `pio test -e native`
Expected: PASS

- [ ] **Step 5: Commit AirPlay receiver**

```bash
git add include/airplay_receiver.h src/airplay_receiver.cpp test/test_airplay/test_airplay.cpp
git commit -m "feat: implement AirPlay 1 RAOP audio and metadata receiver"
```

---

### Task 5: Wi-Fi Lyrics Client & LRC Parser

**Files:**
- Create: `include/lyrics_service.h`
- Create: `src/lyrics_service.cpp`
- Test: `test/test_lrc/test_lrc.cpp`

**Interfaces:**
- Consumes: Track Title & Artist strings from BT or AirPlay
- Produces: `bool lrc_parse(const char *raw_lrc, LyricsData *out)`, `const char* lrc_get_line_at(const LyricsData *data, uint32_t ms, int offset_line)`

- [ ] **Step 1: Write test for timestamped LRC parser and binary search lookup**

```cpp
#include <unity.h>
#include "lyrics_service.h"

void test_lrc_timestamp_parsing() {
    const char *sample = "[00:15.30]First line\n[00:20.50]Second line\n[01:05.00]Third line";
    LyricsData lrc;
    TEST_ASSERT_TRUE(lrc_parse(sample, &lrc));
    TEST_ASSERT_EQUAL_UINT32(3, lrc.count);
    TEST_ASSERT_EQUAL_STRING("First line", lrc_get_line_at(&lrc, 16000, 0));
    TEST_ASSERT_EQUAL_STRING("Second line", lrc_get_line_at(&lrc, 22000, 0));
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `pio test -e native`
Expected: FAIL

- [ ] **Step 3: Implement LRC parser and LRCLIB HTTP fetcher**

Implement `lrc_parse` parsing `[mm:ss.xx]` tags, sorting by milliseconds. Implement `lyrics_fetch_async` to query `https://lrclib.net/api/get` on Core 1 without blocking audio.

- [ ] **Step 4: Run test to verify LRC parser passes**

Run: `pio test -e native`
Expected: PASS

- [ ] **Step 5: Commit lyrics service**

```bash
git add include/lyrics_service.h src/lyrics_service.cpp test/test_lrc/test_lrc.cpp
git commit -m "feat: implement LRCLIB HTTP client and synchronized LRC parser"
```

---

### Task 6: LVGL Display UI (Layout A: 5-Line Scrolling Lyrics)

**Files:**
- Create: `include/ui_display.h`
- Create: `src/ui_display.cpp`
- Test: `test/test_ui/test_ui.cpp`

**Interfaces:**
- Consumes: `config.h` (SPI pins), `lyrics_service.h`
- Produces: `void ui_init()`, `void ui_update_track(const char *title, const char *artist, const char *source)`, `void ui_update_progress(uint32_t current_ms, uint32_t total_ms)`

- [ ] **Step 1: Write test for UI state model and text truncation**

```cpp
#include <unity.h>
#include "ui_display.h"

void test_title_truncation_for_small_screen() {
    char buf[32];
    truncate_for_display("This is an extremely long song title that exceeds screen width", buf, sizeof(buf));
    TEST_ASSERT_LESS_OR_EQUAL(31, strlen(buf));
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `pio test -e native`
Expected: FAIL

- [ ] **Step 3: Implement LVGL 8.x UI driver with ST7789/ILI9341**

Build Layout A: Top status bar (source icon, track title, Wi-Fi), central 5-line vertical scrolling lyric container with highlighted active line in cyan (`#00d2ff`), and bottom progress bar with elapsed/total time labels.

- [ ] **Step 4: Run test to verify UI state helpers pass**

Run: `pio test -e native`
Expected: PASS

- [ ] **Step 5: Commit display UI**

```bash
git add include/ui_display.h src/ui_display.cpp test/test_ui/test_ui.cpp
git commit -m "feat: implement LVGL Layout A 5-line scrolling lyrics screen"
```

---

### Task 7: System Integration & Audio/Lyrics Coordination

**Files:**
- Modify: `src/main.cpp`
- Test: `test/test_integration/test_state_machine.cpp`

**Interfaces:**
- Consumes: `audio_i2s`, `bt_sink`, `airplay_receiver`, `lyrics_service`, `ui_display`
- Produces: Unified FreeRTOS dual-core firmware loop

- [ ] **Step 1: Write test for audio source arbitration state machine**

```cpp
#include <unity.h>
#include "state_arbiter.h"

void test_airplay_preempts_bluetooth() {
    AudioSource active = SOURCE_BLUETOOTH;
    active = arbitrate_audio_event(active, EVENT_AIRPLAY_START);
    TEST_ASSERT_EQUAL(SOURCE_AIRPLAY, active);
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `pio test -e native`
Expected: FAIL

- [ ] **Step 3: Implement system coordination in `src/main.cpp`**

Initialize PSRAM, start Wi-Fi, launch I2S driver, start Bluetooth sink and AirPlay receiver on Core 0. Launch LVGL display update loop and async HTTP lyrics worker on Core 1.

- [ ] **Step 4: Run test to verify state arbitration passes**

Run: `pio test -e native`
Expected: PASS

- [ ] **Step 5: Commit system integration**

```bash
git add src/main.cpp test/test_integration/test_state_machine.cpp
git commit -m "feat: integrate dual-core FreeRTOS audio, lyrics, and display coordination"
```
