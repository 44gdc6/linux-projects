#include "device/sensor_common.h"

double lm75_raw_to_celsius(short raw_value)
{
    return raw_value * 0.5;
}

const char *temp_source_name(temp_source_t source)
{
    switch (source) {
    case TEMP_SOURCE_LM75:
        return "lm75";
    case TEMP_SOURCE_MOCK:
        return "mock";
    default:
        return "unknown";
    }
}

alcohol_level_t alcohol_level_from_raw(int raw_value, int low_threshold, int high_threshold)
{
    if (raw_value >= high_threshold) {
        return ALCOHOL_LEVEL_HIGH;
    }

    if (raw_value >= low_threshold) {
        return ALCOHOL_LEVEL_MEDIUM;
    }

    return ALCOHOL_LEVEL_LOW;
}

const char *alcohol_level_name(alcohol_level_t level)
{
    switch (level) {
    case ALCOHOL_LEVEL_LOW:
        return "LOW";
    case ALCOHOL_LEVEL_MEDIUM:
        return "MEDIUM";
    case ALCOHOL_LEVEL_HIGH:
        return "HIGH";
    default:
        return "UNKNOWN";
    }
}

const char *motion_state_name(motion_state_t state)
{
    switch (state) {
    case MOTION_STATE_NORMAL:
        return "NORMAL";
    case MOTION_STATE_SHAKE:
        return "SHAKE";
    case MOTION_STATE_VIBRATION:
        return "VIBRATION";
    case MOTION_STATE_TILT:
        return "TILT";
    default:
        return "UNKNOWN";
    }
}
