#include <assert.h>
#include <math.h>
#include <string.h>

#include "device/sensor_common.h"

static void test_lm75_raw_to_celsius(void)
{
    assert(fabs(lm75_raw_to_celsius(40) - 20.0) < 0.0001);
    assert(fabs(lm75_raw_to_celsius(0) - 0.0) < 0.0001);
}

static void test_temp_source_name(void)
{
    assert(strcmp(temp_source_name(TEMP_SOURCE_LM75), "lm75") == 0);
    assert(strcmp(temp_source_name(TEMP_SOURCE_MOCK), "mock") == 0);
}

static void test_alcohol_level_name(void)
{
    assert(strcmp(alcohol_level_name(ALCOHOL_LEVEL_LOW), "LOW") == 0);
    assert(strcmp(alcohol_level_name(ALCOHOL_LEVEL_MEDIUM), "MEDIUM") == 0);
    assert(strcmp(alcohol_level_name(ALCOHOL_LEVEL_HIGH), "HIGH") == 0);
}

static void test_alcohol_level_from_raw(void)
{
    assert(alcohol_level_from_raw(0, 1200, 1800) == ALCOHOL_LEVEL_LOW);
    assert(alcohol_level_from_raw(1199, 1200, 1800) == ALCOHOL_LEVEL_LOW);
    assert(alcohol_level_from_raw(1200, 1200, 1800) == ALCOHOL_LEVEL_MEDIUM);
    assert(alcohol_level_from_raw(1799, 1200, 1800) == ALCOHOL_LEVEL_MEDIUM);
    assert(alcohol_level_from_raw(1800, 1200, 1800) == ALCOHOL_LEVEL_HIGH);
}

static void test_motion_state_name(void)
{
    assert(strcmp(motion_state_name(MOTION_STATE_NORMAL), "NORMAL") == 0);
    assert(strcmp(motion_state_name(MOTION_STATE_SHAKE), "SHAKE") == 0);
    assert(strcmp(motion_state_name(MOTION_STATE_VIBRATION), "VIBRATION") == 0);
    assert(strcmp(motion_state_name(MOTION_STATE_TILT), "TILT") == 0);
}

int main(void)
{
    test_lm75_raw_to_celsius();
    test_temp_source_name();
    test_alcohol_level_name();
    test_alcohol_level_from_raw();
    test_motion_state_name();
    return 0;
}
