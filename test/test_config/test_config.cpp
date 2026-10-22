#include <unity.h>
#include "config.h"

void setUp(void) {
}

void tearDown(void) {
}

void test_forbidden_pins_not_used(void) {
    // PSRAM pins GPIO 16 and GPIO 17 must never be used
    TEST_ASSERT_NOT_EQUAL(16, I2S_BCLK_PIN);
    TEST_ASSERT_NOT_EQUAL(16, I2S_LRC_PIN);
    TEST_ASSERT_NOT_EQUAL(16, I2S_DIN_PIN);
    TEST_ASSERT_NOT_EQUAL(17, I2S_BCLK_PIN);
    TEST_ASSERT_NOT_EQUAL(17, I2S_LRC_PIN);
    TEST_ASSERT_NOT_EQUAL(17, I2S_DIN_PIN);

    TEST_ASSERT_NOT_EQUAL(16, TFT_CS_PIN);
    TEST_ASSERT_NOT_EQUAL(16, TFT_DC_PIN);
    TEST_ASSERT_NOT_EQUAL(16, TFT_RST_PIN);
    TEST_ASSERT_NOT_EQUAL(16, TFT_SCK_PIN);
    TEST_ASSERT_NOT_EQUAL(16, TFT_MOSI_PIN);
    TEST_ASSERT_NOT_EQUAL(16, TFT_BLK_PIN);

    TEST_ASSERT_NOT_EQUAL(17, TFT_CS_PIN);
    TEST_ASSERT_NOT_EQUAL(17, TFT_DC_PIN);
    TEST_ASSERT_NOT_EQUAL(17, TFT_RST_PIN);
    TEST_ASSERT_NOT_EQUAL(17, TFT_SCK_PIN);
    TEST_ASSERT_NOT_EQUAL(17, TFT_MOSI_PIN);
    TEST_ASSERT_NOT_EQUAL(17, TFT_BLK_PIN);
}

void test_i2s_pin_assignments(void) {
    TEST_ASSERT_EQUAL_INT(25, I2S_BCLK_PIN);
    TEST_ASSERT_EQUAL_INT(26, I2S_LRC_PIN);
    TEST_ASSERT_EQUAL_INT(27, I2S_DIN_PIN);
}

void test_spi_pin_assignments(void) {
    TEST_ASSERT_EQUAL_INT(5, TFT_CS_PIN);
    TEST_ASSERT_EQUAL_INT(2, TFT_DC_PIN);
    TEST_ASSERT_EQUAL_INT(4, TFT_RST_PIN);
    TEST_ASSERT_EQUAL_INT(18, TFT_SCK_PIN);
    TEST_ASSERT_EQUAL_INT(23, TFT_MOSI_PIN);
    TEST_ASSERT_EQUAL_INT(21, TFT_BLK_PIN);
}

#if defined(ARDUINO)
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_forbidden_pins_not_used);
    RUN_TEST(test_i2s_pin_assignments);
    RUN_TEST(test_spi_pin_assignments);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_forbidden_pins_not_used);
    RUN_TEST(test_i2s_pin_assignments);
    RUN_TEST(test_spi_pin_assignments);
    return UNITY_END();
}
#endif
