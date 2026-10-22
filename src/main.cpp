#include <Arduino.h>
#include "config.h"

void setup() {
    Serial.begin(115200);
    delay(1000);

    log_i("ESP32 AirPlay & Bluetooth Lyrics Speaker Initializing...");

    // Verify PSRAM availability (crucial for ESP32-WROVER-E audio and UI buffers)
    if (psramInit()) {
        log_i("PSRAM initialized successfully.");
        log_i("PSRAM size: %d", ESP.getPsramSize());
        log_i("Free PSRAM: %d", ESP.getFreePsram());
    } else {
        log_e("PSRAM initialization failed! Check board configuration.");
    }

    log_i("I2S Pinout: BCLK=%d, LRC=%d, DIN=%d", I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN);
    log_i("TFT Pinout: CS=%d, DC=%d, RST=%d, SCK=%d, MOSI=%d, BLK=%d",
          TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN, TFT_SCK_PIN, TFT_MOSI_PIN, TFT_BLK_PIN);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
