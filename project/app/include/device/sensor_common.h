#ifndef __SENSOR_COMMON_H__
#define __SENSOR_COMMON_H__

typedef enum {
    TEMP_SOURCE_NONE = 0,
    TEMP_SOURCE_LM75,
    TEMP_SOURCE_MOCK,
} temp_source_t;

typedef enum {
    ALCOHOL_LEVEL_LOW = 0,
    ALCOHOL_LEVEL_MEDIUM,
    ALCOHOL_LEVEL_HIGH,
} alcohol_level_t;

typedef enum {
    MOTION_STATE_NORMAL = 0,
    MOTION_STATE_SHAKE,
    MOTION_STATE_VIBRATION,
    MOTION_STATE_TILT,
} motion_state_t;

double lm75_raw_to_celsius(short raw_value);
const char *temp_source_name(temp_source_t source);
alcohol_level_t alcohol_level_from_raw(int raw_value, int low_threshold, int high_threshold);
const char *alcohol_level_name(alcohol_level_t level);
const char *motion_state_name(motion_state_t state);

#endif
