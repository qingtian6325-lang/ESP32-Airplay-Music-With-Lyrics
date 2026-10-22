#include "ui_display.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// UI State Storage & Synchronization
// =============================================================================

static UIState s_ui_state;
static LyricsData s_current_lyrics;
static bool s_ui_initialized = false;

// Forward declaration of hardware update hooks
static void hw_update_track_view(void);
static void hw_update_progress_view(void);
static void hw_update_lyrics_view(void);
static void hw_update_wifi_view(void);

// =============================================================================
// Pure Formatting and Window Helpers (Host & Target)
// =============================================================================

void format_time_ms(uint32_t ms, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }

    uint32_t total_sec = ms / 1000;
    uint32_t hours = total_sec / 3600;
    uint32_t mins = (total_sec % 3600) / 60;
    uint32_t secs = total_sec % 60;

    if (hours > 0) {
        snprintf(out, out_len, "%02u:%02u:%02u", (unsigned int)hours, (unsigned int)mins, (unsigned int)secs);
    } else {
        snprintf(out, out_len, "%02u:%02u", (unsigned int)mins, (unsigned int)secs);
    }
}

void truncate_for_display(const char *in, char *out, size_t max_len) {
    if (!out || max_len == 0) {
        return;
    }
    if (!in) {
        out[0] = '\0';
        return;
    }

    size_t in_len = strlen(in);
    if (in_len < max_len) {
        memcpy(out, in, in_len + 1);
    } else {
        if (max_len > 3) {
            size_t prefix_len = max_len - 4;
            memcpy(out, in, prefix_len);
            out[prefix_len] = '.';
            out[prefix_len + 1] = '.';
            out[prefix_len + 2] = '.';
            out[prefix_len + 3] = '\0';
        } else {
            memcpy(out, in, max_len - 1);
            out[max_len - 1] = '\0';
        }
    }
}

void compute_lyric_window(const LyricsData *lyrics, uint32_t current_ms, char window[5][128], int *active_idx) {
    if (!window) {
        return;
    }

    for (int i = 0; i < 5; i++) {
        window[i][0] = '\0';
    }

    if (!lyrics || !lyrics->loaded || lyrics->count == 0) {
        if (active_idx) {
            *active_idx = -1;
        }
        strncpy(window[2], "No lyrics available", sizeof(window[2]) - 1);
        window[2][sizeof(window[2]) - 1] = '\0';
        return;
    }

    int active = lrc_get_active_index_at(lyrics, current_ms);
    if (active_idx) {
        *active_idx = active;
    }

    for (int i = 0; i < 5; i++) {
        int offset = i - 2; // -2, -1, 0, +1, +2
        int target = active + offset;
        if (target >= 0 && target < (int)lyrics->count) {
            strncpy(window[i], lyrics->lines[target].text, sizeof(window[i]) - 1);
            window[i][sizeof(window[i]) - 1] = '\0';
        } else {
            window[i][0] = '\0';
        }
    }
}

// =============================================================================
// UI State Accessors & Mutators
// =============================================================================

void ui_update_track(const char *title, const char *artist, AudioSourceType source) {
    if (title) {
        truncate_for_display(title, s_ui_state.title, sizeof(s_ui_state.title));
    } else {
        s_ui_state.title[0] = '\0';
    }

    if (artist) {
        truncate_for_display(artist, s_ui_state.artist, sizeof(s_ui_state.artist));
    } else {
        s_ui_state.artist[0] = '\0';
    }

    s_ui_state.source = source;
    hw_update_track_view();
}

void ui_update_progress(uint32_t current_ms, uint32_t total_ms) {
    s_ui_state.current_ms = current_ms;
    s_ui_state.total_ms = total_ms;

    compute_lyric_window(&s_current_lyrics, current_ms, s_ui_state.lyric_window, &s_ui_state.active_lyric_index);
    hw_update_progress_view();
    hw_update_lyrics_view();
}

void ui_update_lyrics(const LyricsData *lyrics) {
    if (lyrics) {
        memcpy(&s_current_lyrics, lyrics, sizeof(LyricsData));
    } else {
        memset(&s_current_lyrics, 0, sizeof(LyricsData));
    }

    compute_lyric_window(&s_current_lyrics, s_ui_state.current_ms, s_ui_state.lyric_window, &s_ui_state.active_lyric_index);
    hw_update_lyrics_view();
}

void ui_set_wifi_status(bool connected) {
    s_ui_state.wifi_connected = connected;
    hw_update_wifi_view();
}

const UIState* ui_get_state(void) {
    return &s_ui_state;
}

// =============================================================================
// Embedded Hardware Implementation: TFT_eSPI + LVGL Layout A (Core 1 / CORE_UI)
// =============================================================================

#if (defined(ESP_PLATFORM) || defined(ARDUINO)) && !defined(ARDUINO_ARCH_NATIVE)

#include <Arduino.h>
#include "config.h"
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#define DISP_HOR_RES 320
#define DISP_VER_RES 240

static TFT_eSPI s_tft = TFT_eSPI();
static lv_disp_draw_buf_t s_disp_draw_buf;
static lv_color_t s_disp_buf[DISP_HOR_RES * 20]; // 320 x 20 lines buffer (~12.8 KB)
static lv_disp_drv_t s_disp_drv;

static SemaphoreHandle_t s_lvgl_mutex = NULL;
static TaskHandle_t s_ui_task_handle = NULL;
static volatile bool s_ui_task_running = false;

// Layout A Visual Objects
static lv_obj_t *s_status_bar = NULL;
static lv_obj_t *s_lbl_source = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_lbl_wifi = NULL;

static lv_obj_t *s_lyrics_cont = NULL;
static lv_obj_t *s_lbl_lyrics[5] = {NULL};

static lv_obj_t *s_bottom_bar = NULL;
static lv_obj_t *s_lbl_elapsed = NULL;
static lv_obj_t *s_lbl_total = NULL;
static lv_obj_t *s_bar_progress = NULL;

static void lvgl_disp_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);
    s_tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    s_tft.endWrite();

    lv_disp_flush_ready(disp);
}

static void create_layout_a(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a0e), 0);

    // 1. Top Status Bar (Height: 28px)
    s_status_bar = lv_obj_create(scr);
    lv_obj_set_size(s_status_bar, DISP_HOR_RES, 28);
    lv_obj_align(s_status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_status_bar, lv_color_hex(0x16161d), 0);
    lv_obj_set_style_pad_all(s_status_bar, 0, 0);
    lv_obj_set_style_border_width(s_status_bar, 0, 0);
    lv_obj_clear_flag(s_status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_source = lv_label_create(s_status_bar);
    lv_obj_align(s_lbl_source, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_text_color(s_lbl_source, lv_color_hex(0x888888), 0);
    lv_label_set_text(s_lbl_source, "IDLE");

    s_lbl_title = lv_label_create(s_status_bar);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(s_lbl_title, 190);
    lv_obj_set_style_text_align(s_lbl_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_lbl_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_lbl_title, "Ready to Play");

    s_lbl_wifi = lv_label_create(s_status_bar);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(0x666666), 0);
    lv_label_set_text(s_lbl_wifi, "WiFi OFF");

    // 2. Central 5-Line Scrolling Lyrics Container (Height: 172px, y: 28..200)
    s_lyrics_cont = lv_obj_create(scr);
    lv_obj_set_size(s_lyrics_cont, DISP_HOR_RES, 172);
    lv_obj_align(s_lyrics_cont, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(s_lyrics_cont, lv_color_hex(0x0a0a0e), 0);
    lv_obj_set_style_pad_all(s_lyrics_cont, 0, 0);
    lv_obj_set_style_border_width(s_lyrics_cont, 0, 0);
    lv_obj_clear_flag(s_lyrics_cont, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t y_offsets[5] = { 6, 38, 70, 104, 136 };
    // Palette for lines: Line 2 is highlighted cyan (#00D2FF), adjacent are brighter, outer are dim
    const uint32_t colors[5] = { 0x555555, 0x999999, 0x00D2FF, 0x999999, 0x555555 };

    for (int i = 0; i < 5; i++) {
        s_lbl_lyrics[i] = lv_label_create(s_lyrics_cont);
        lv_obj_set_width(s_lbl_lyrics[i], 310);
        lv_obj_set_pos(s_lbl_lyrics[i], 5, y_offsets[i]);
        lv_obj_set_style_text_align(s_lbl_lyrics[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_lbl_lyrics[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(s_lbl_lyrics[i], lv_color_hex(colors[i]), 0);
        lv_label_set_text(s_lbl_lyrics[i], (i == 2) ? "No lyrics available" : "");
    }

    // 3. Bottom Playback Progress Bar (Height: 40px, y: 200..240)
    s_bottom_bar = lv_obj_create(scr);
    lv_obj_set_size(s_bottom_bar, DISP_HOR_RES, 40);
    lv_obj_align(s_bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bottom_bar, lv_color_hex(0x16161d), 0);
    lv_obj_set_style_pad_all(s_bottom_bar, 0, 0);
    lv_obj_set_style_border_width(s_bottom_bar, 0, 0);
    lv_obj_clear_flag(s_bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_elapsed = lv_label_create(s_bottom_bar);
    lv_obj_align(s_lbl_elapsed, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_text_color(s_lbl_elapsed, lv_color_hex(0x888888), 0);
    lv_label_set_text(s_lbl_elapsed, "00:00");

    s_lbl_total = lv_label_create(s_bottom_bar);
    lv_obj_align(s_lbl_total, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_text_color(s_lbl_total, lv_color_hex(0x888888), 0);
    lv_label_set_text(s_lbl_total, "00:00");

    s_bar_progress = lv_bar_create(s_bottom_bar);
    lv_obj_set_size(s_bar_progress, 180, 8);
    lv_obj_align(s_bar_progress, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_range(s_bar_progress, 0, 1000);
    lv_bar_set_value(s_bar_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_progress, lv_color_hex(0x33333e), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_progress, lv_color_hex(0x00D2FF), LV_PART_INDICATOR);
}

static void ui_render_task_entry(void *pvParameters) {
    (void)pvParameters;
    while (s_ui_task_running) {
        ui_task_tick(30);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    vTaskDelete(NULL);
}

static void hw_update_track_view(void) {
    if (!s_lvgl_mutex || !s_lbl_title) return;
    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_ui_state.title[0] != '\0') {
            lv_label_set_text(s_lbl_title, s_ui_state.title);
        } else {
            lv_label_set_text(s_lbl_title, "Ready to Play");
        }

        switch (s_ui_state.source) {
            case SOURCE_BLUETOOTH:
                lv_label_set_text(s_lbl_source, "BT");
                lv_obj_set_style_text_color(s_lbl_source, lv_color_hex(0x00D2FF), 0);
                break;
            case SOURCE_AIRPLAY:
                lv_label_set_text(s_lbl_source, "AirPlay");
                lv_obj_set_style_text_color(s_lbl_source, lv_color_hex(0x1DB954), 0);
                break;
            default:
                lv_label_set_text(s_lbl_source, "IDLE");
                lv_obj_set_style_text_color(s_lbl_source, lv_color_hex(0x888888), 0);
                break;
        }
        xSemaphoreGive(s_lvgl_mutex);
    }
}

static void hw_update_progress_view(void) {
    if (!s_lvgl_mutex || !s_bar_progress) return;
    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        char buf[16];
        format_time_ms(s_ui_state.current_ms, buf, sizeof(buf));
        lv_label_set_text(s_lbl_elapsed, buf);

        format_time_ms(s_ui_state.total_ms, buf, sizeof(buf));
        lv_label_set_text(s_lbl_total, buf);

        int32_t pct = 0;
        if (s_ui_state.total_ms > 0) {
            pct = (int32_t)(((uint64_t)s_ui_state.current_ms * 1000ULL) / s_ui_state.total_ms);
            if (pct > 1000) pct = 1000;
        }
        lv_bar_set_value(s_bar_progress, pct, LV_ANIM_OFF);
        xSemaphoreGive(s_lvgl_mutex);
    }
}

static void hw_update_lyrics_view(void) {
    if (!s_lvgl_mutex || !s_lbl_lyrics[0]) return;
    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < 5; i++) {
            lv_label_set_text(s_lbl_lyrics[i], s_ui_state.lyric_window[i]);
        }
        xSemaphoreGive(s_lvgl_mutex);
    }
}

static void hw_update_wifi_view(void) {
    if (!s_lvgl_mutex || !s_lbl_wifi) return;
    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_ui_state.wifi_connected) {
            lv_label_set_text(s_lbl_wifi, "WiFi");
            lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(0x1DB954), 0);
        } else {
            lv_label_set_text(s_lbl_wifi, "No WiFi");
            lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(0x666666), 0);
        }
        xSemaphoreGive(s_lvgl_mutex);
    }
}

bool ui_init(void) {
    memset(&s_ui_state, 0, sizeof(s_ui_state));
    memset(&s_current_lyrics, 0, sizeof(s_current_lyrics));
    s_ui_state.source = SOURCE_NONE;
    s_ui_state.active_lyric_index = -1;
    s_ui_state.wifi_connected = false;
    compute_lyric_window(&s_current_lyrics, 0, s_ui_state.lyric_window, &s_ui_state.active_lyric_index);

    if (s_ui_initialized) {
        return true;
    }

    if (!s_lvgl_mutex) {
        s_lvgl_mutex = xSemaphoreCreateMutex();
    }

    // Hardware TFT Backlight control
    pinMode(TFT_BLK_PIN, OUTPUT);
    digitalWrite(TFT_BLK_PIN, HIGH);

    // TFT Driver Init
    s_tft.init();
    s_tft.setRotation(1); // 320x240 Landscape

    // LVGL Init
    lv_init();
    lv_disp_draw_buf_init(&s_disp_draw_buf, s_disp_buf, NULL, DISP_HOR_RES * 20);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = DISP_HOR_RES;
    s_disp_drv.ver_res = DISP_VER_RES;
    s_disp_drv.flush_cb = lvgl_disp_flush_cb;
    s_disp_drv.draw_buf = &s_disp_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    // Construct Layout A Widgets
    create_layout_a();

    s_ui_initialized = true;
    s_ui_task_running = true;

    // Launch UI render task strictly on CORE_UI (Core 1)
    xTaskCreatePinnedToCore(
        ui_render_task_entry,
        "ui_render_task",
        4096,
        NULL,
        2,
        &s_ui_task_handle,
        CORE_UI
    );

    return true;
}

void ui_task_tick(uint32_t delta_ms) {
    if (s_lvgl_mutex && xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        lv_tick_inc(delta_ms);
        lv_timer_handler();
        xSemaphoreGive(s_lvgl_mutex);
    }
}

#else

// =============================================================================
// Host Native Mock Implementation for Unit Testing
// =============================================================================

static void hw_update_track_view(void) {}
static void hw_update_progress_view(void) {}
static void hw_update_lyrics_view(void) {}
static void hw_update_wifi_view(void) {}

bool ui_init(void) {
    memset(&s_ui_state, 0, sizeof(s_ui_state));
    memset(&s_current_lyrics, 0, sizeof(s_current_lyrics));
    s_ui_state.source = SOURCE_NONE;
    s_ui_state.active_lyric_index = -1;
    s_ui_state.wifi_connected = false;
    compute_lyric_window(&s_current_lyrics, 0, s_ui_state.lyric_window, &s_ui_state.active_lyric_index);
    s_ui_initialized = true;
    return true;
}

void ui_task_tick(uint32_t delta_ms) {
    (void)delta_ms;
}

#endif
