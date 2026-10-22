#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "audio_i2s.h"

void setUp(void) {
    audio_i2s_set_volume(100);
}

void tearDown(void) {
}

void test_volume_scaling_zero(void) {
    TEST_ASSERT_EQUAL_INT16(0, audio_apply_volume(1000, 0));
    TEST_ASSERT_EQUAL_INT16(0, audio_apply_volume(-1000, 0));
    TEST_ASSERT_EQUAL_INT16(0, audio_apply_volume(32767, 0));
    TEST_ASSERT_EQUAL_INT16(0, audio_apply_volume(-32768, 0));
}

void test_volume_scaling_max(void) {
    TEST_ASSERT_EQUAL_INT16(1000, audio_apply_volume(1000, 100));
    TEST_ASSERT_EQUAL_INT16(-1000, audio_apply_volume(-1000, 100));
    TEST_ASSERT_EQUAL_INT16(32767, audio_apply_volume(32767, 100));
    TEST_ASSERT_EQUAL_INT16(-32768, audio_apply_volume(-32768, 100));
}

void test_volume_scaling_half(void) {
    TEST_ASSERT_EQUAL_INT16(500, audio_apply_volume(1000, 50));
    TEST_ASSERT_EQUAL_INT16(-500, audio_apply_volume(-1000, 50));
    TEST_ASSERT_EQUAL_INT16(16383, audio_apply_volume(32767, 50));
    TEST_ASSERT_EQUAL_INT16(-16384, audio_apply_volume(-32768, 50));
}

void test_volume_scaling_arbitrary(void) {
    TEST_ASSERT_EQUAL_INT16(10, audio_apply_volume(1000, 1));
    TEST_ASSERT_EQUAL_INT16(990, audio_apply_volume(1000, 99));
    TEST_ASSERT_EQUAL_INT16(-10, audio_apply_volume(-1000, 1));
    TEST_ASSERT_EQUAL_INT16(-990, audio_apply_volume(-1000, 99));
}

void test_volume_scaling_clamp(void) {
    // Values > 100% must clamp to 100% to avoid overflow/clipping
    TEST_ASSERT_EQUAL_INT16(1000, audio_apply_volume(1000, 150));
    TEST_ASSERT_EQUAL_INT16(-1000, audio_apply_volume(-1000, 200));
    TEST_ASSERT_EQUAL_INT16(32767, audio_apply_volume(32767, 255));
    TEST_ASSERT_EQUAL_INT16(-32768, audio_apply_volume(-32768, 255));
}

void test_volume_buffer_scaling(void) {
    int16_t samples[5] = {0, 1000, -1000, 32767, -32768};
    audio_apply_volume_buffer(samples, 5, 50);

    TEST_ASSERT_EQUAL_INT16(0, samples[0]);
    TEST_ASSERT_EQUAL_INT16(500, samples[1]);
    TEST_ASSERT_EQUAL_INT16(-500, samples[2]);
    TEST_ASSERT_EQUAL_INT16(16383, samples[3]);
    TEST_ASSERT_EQUAL_INT16(-16384, samples[4]);
}

void test_volume_buffer_zero(void) {
    int16_t samples[4] = {100, -200, 300, -400};
    audio_apply_volume_buffer(samples, 4, 0);

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT16(0, samples[i]);
    }
}

void test_volume_buffer_unchanged_at_100(void) {
    int16_t samples[4] = {123, -456, 789, -1000};
    audio_apply_volume_buffer(samples, 4, 100);

    TEST_ASSERT_EQUAL_INT16(123, samples[0]);
    TEST_ASSERT_EQUAL_INT16(-456, samples[1]);
    TEST_ASSERT_EQUAL_INT16(789, samples[2]);
    TEST_ASSERT_EQUAL_INT16(-1000, samples[3]);
}

void test_volume_buffer_null_and_zero_safety(void) {
    // Should not crash or fault
    audio_apply_volume_buffer(NULL, 10, 50);

    int16_t sample = 42;
    audio_apply_volume_buffer(&sample, 0, 50);
    TEST_ASSERT_EQUAL_INT16(42, sample);
}

void test_volume_get_set(void) {
    audio_i2s_set_volume(80);
    TEST_ASSERT_EQUAL_UINT8(80, audio_i2s_get_volume());

    audio_i2s_set_volume(0);
    TEST_ASSERT_EQUAL_UINT8(0, audio_i2s_get_volume());

    audio_i2s_set_volume(150); // Clamped to 100
    TEST_ASSERT_EQUAL_UINT8(100, audio_i2s_get_volume());
}

void test_audio_i2s_init_and_write(void) {
    TEST_ASSERT_TRUE(audio_i2s_init());

    int16_t test_pcm[4] = {100, 200, 300, 400};
    size_t written = audio_i2s_write(reinterpret_cast<const uint8_t*>(test_pcm), sizeof(test_pcm));
    TEST_ASSERT_EQUAL_UINT32(sizeof(test_pcm), written);

    // Null and 0 checks
    TEST_ASSERT_EQUAL_UINT32(0, audio_i2s_write(NULL, 100));
    TEST_ASSERT_EQUAL_UINT32(0, audio_i2s_write(reinterpret_cast<const uint8_t*>(test_pcm), 0));
}

#if defined(ARDUINO)
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_volume_scaling_zero);
    RUN_TEST(test_volume_scaling_max);
    RUN_TEST(test_volume_scaling_half);
    RUN_TEST(test_volume_scaling_arbitrary);
    RUN_TEST(test_volume_scaling_clamp);
    RUN_TEST(test_volume_buffer_scaling);
    RUN_TEST(test_volume_buffer_zero);
    RUN_TEST(test_volume_buffer_unchanged_at_100);
    RUN_TEST(test_volume_buffer_null_and_zero_safety);
    RUN_TEST(test_volume_get_set);
    RUN_TEST(test_audio_i2s_init_and_write);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_volume_scaling_zero);
    RUN_TEST(test_volume_scaling_max);
    RUN_TEST(test_volume_scaling_half);
    RUN_TEST(test_volume_scaling_arbitrary);
    RUN_TEST(test_volume_scaling_clamp);
    RUN_TEST(test_volume_buffer_scaling);
    RUN_TEST(test_volume_buffer_zero);
    RUN_TEST(test_volume_buffer_unchanged_at_100);
    RUN_TEST(test_volume_buffer_null_and_zero_safety);
    RUN_TEST(test_volume_get_set);
    RUN_TEST(test_audio_i2s_init_and_write);
    return UNITY_END();
}
#endif
