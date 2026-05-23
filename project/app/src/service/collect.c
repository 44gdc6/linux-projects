#include "service/collect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config/app_config.h"
#include "core/log.h"
#include "core/mailbox.h"
#include "device/beep_control.h"
#include "device/sensor_adxl345.h"
#include "device/sensor_dht11.h"
#include "device/sensor_lm75.h"
#include "device/sensor_mq3.h"
#include "service/weather.h"

#define SENSOR_REGISTRY_CAPACITY 8

static void fill_mock_channels(data_t *sample)
{
    sample->light = rand() % 10000 / 100.0;
    sample->smoke = rand() % 10000 / 100.0;
}

static void fill_mock_temp(data_t *sample)
{
    sample->temp = rand() % 10000 / 100.0;
    sample->temp_valid = 1;
    sample->temp_source = TEMP_SOURCE_MOCK;
}

static int register_sensor(sensor_registration_t *registry,
                           size_t capacity,
                           const sensor_ops_t *ops)
{
    size_t index = 0;

    if (registry == NULL || ops == NULL) {
        return -1;
    }

    for (index = 0; index < capacity; ++index) {
        if (registry[index].ops == NULL) {
            registry[index].ops = ops;
            registry[index].enabled = 1;
            registry[index].ctx = NULL;
            if (ops->init != NULL && ops->init(&registry[index].ctx) != 0) {
                registry[index].enabled = 0;
                LOG_ERROR("sensor init failed: %s", ops->name == NULL ? "unknown" : ops->name);
            }
            return 0;
        }
    }

    return -1;
}

static void unregister_sensors(sensor_registration_t *registry, size_t capacity)
{
    size_t index = 0;

    for (index = 0; index < capacity; ++index) {
        if (registry[index].ops != NULL && registry[index].ops->deinit != NULL) {
            registry[index].ops->deinit(registry[index].ctx);
        }
    }
}

static void collect_temperature(sensor_registration_t *registry,
                                size_t capacity,
                                data_t *sample,
                                int *mock_active)
{
    size_t index = 0;

    for (index = 0; index < capacity; ++index) {
        if (registry[index].ops == NULL || registry[index].enabled == 0) {
            continue;
        }

        if (registry[index].ops->read != NULL &&
            registry[index].ops->read(registry[index].ctx, sample) == 0 &&
            sample->temp_valid) {
            if (*mock_active != 0) {
                LOG_INFO("sensor data recovered, leave mock fallback");
                *mock_active = 0;
            }
            return;
        }
    }

    fill_mock_temp(sample);
    if (*mock_active == 0) {
        LOG_WARN("all temperature sensors unavailable, fallback to mock temperature");
        *mock_active = 1;
    }
}

static void collect_alcohol(void *mq3_ctx, data_t *sample, int *sensor_online)
{
    if (mq3_sensor_read(mq3_ctx, sample) == 0 && sample->alcohol_valid) {
        sample->alcohol_level = alcohol_level_from_raw(sample->alcohol_raw,
                                                       ALCOHOL_LOW_THRESHOLD,
                                                       ALCOHOL_HIGH_THRESHOLD);
        if (*sensor_online == 0) {
            LOG_INFO("mq3 sensor recovered");
            *sensor_online = 1;
        }
        return;
    }

    sample->alcohol_valid = 0;
    sample->alcohol_alarm = 0;
    if (*sensor_online != 0) {
        LOG_WARN("mq3 sensor unavailable");
        *sensor_online = 0;
    }
}

static void collect_humidity(void *dht11_ctx, data_t *sample, int *sensor_online)
{
    if (dht11_sensor_read(dht11_ctx, sample) == 0 && sample->hum_valid) {
        if (*sensor_online == 0) {
            LOG_INFO("dht11 sensor recovered");
            *sensor_online = 1;
        }
        return;
    }

    sample->hum_valid = 0;
    if (*sensor_online != 0) {
        LOG_WARN("dht11 sensor unavailable");
        *sensor_online = 0;
    }
}

static void collect_motion(void *adxl_ctx,
                           data_t *sample,
                           int *sensor_online,
                           int *prev_valid,
                           int *prev_x,
                           int *prev_y,
                           int *prev_z)
{
    int delta_x = 0;
    int delta_y = 0;
    int delta_z = 0;
    int max_delta = 0;

    if (adxl345_sensor_read(adxl_ctx, sample) == 0 && sample->accel_valid) {
        if (*sensor_online == 0) {
            LOG_INFO("adxl345 sensor recovered");
            *sensor_online = 1;
        }

        sample->motion_state = MOTION_STATE_NORMAL;
        if (*prev_valid != 0) {
            delta_x = abs(sample->accel_x - *prev_x);
            delta_y = abs(sample->accel_y - *prev_y);
            delta_z = abs(sample->accel_z - *prev_z);
            max_delta = delta_x;
            if (delta_y > max_delta) {
                max_delta = delta_y;
            }
            if (delta_z > max_delta) {
                max_delta = delta_z;
            }
        }

        if (abs(sample->accel_x) >= MOTION_TILT_AXIS_THRESHOLD ||
            abs(sample->accel_y) >= MOTION_TILT_AXIS_THRESHOLD) {
            sample->motion_state = MOTION_STATE_TILT;
        } else if (max_delta >= MOTION_VIBRATION_DELTA_THRESHOLD) {
            sample->motion_state = MOTION_STATE_VIBRATION;
        } else if (max_delta >= MOTION_SHAKE_DELTA_THRESHOLD) {
            sample->motion_state = MOTION_STATE_SHAKE;
        }

        *prev_x = sample->accel_x;
        *prev_y = sample->accel_y;
        *prev_z = sample->accel_z;
        *prev_valid = 1;
        return;
    }

    sample->accel_valid = 0;
    sample->motion_state = MOTION_STATE_NORMAL;
    sample->motion_alarm = 0;
    *prev_valid = 0;
    if (*sensor_online != 0) {
        LOG_WARN("adxl345 sensor unavailable");
        *sensor_online = 0;
    }
}

static void collect_weather(data_t *sample)
{
    weather_snapshot_t snapshot;

    if (weather_cache_get_snapshot(&snapshot) != 0 || !snapshot.valid) {
        sample->weather_valid = 0;
        sample->weather_day[0] = '\0';
        sample->weather_week[0] = '\0';
        sample->weather_city[0] = '\0';
        sample->weather_text[0] = '\0';
        sample->weather_temp[0] = '\0';
        return;
    }

    sample->weather_valid = 1;
    snprintf(sample->weather_day, sizeof(sample->weather_day), "%s", snapshot.day);
    snprintf(sample->weather_week, sizeof(sample->weather_week), "%s", snapshot.week);
    snprintf(sample->weather_city, sizeof(sample->weather_city), "%s", snapshot.city);
    snprintf(sample->weather_text, sizeof(sample->weather_text), "%s", snapshot.weather);
    snprintf(sample->weather_temp, sizeof(sample->weather_temp), "%s", snapshot.temperature);
}

static void update_alcohol_alarm(data_t *sample,
                                 int *alarm_active,
                                 int *high_count,
                                 int *clear_count)
{
    if (sample->alcohol_valid && sample->alcohol_level == ALCOHOL_LEVEL_HIGH) {
        if (*high_count < ALCOHOL_ALARM_ASSERT_COUNT) {
            ++(*high_count);
        }
        *clear_count = 0;
    } else {
        if (*clear_count < ALCOHOL_ALARM_CLEAR_COUNT) {
            ++(*clear_count);
        }
        *high_count = 0;
    }

    if (*alarm_active == 0 && *high_count >= ALCOHOL_ALARM_ASSERT_COUNT) {
        *alarm_active = 1;
        LOG_WARN("alcohol alarm asserted, raw=%d level=%s",
                 sample->alcohol_raw,
                 alcohol_level_name((alcohol_level_t)sample->alcohol_level));
    } else if (*alarm_active != 0 && *clear_count >= ALCOHOL_ALARM_CLEAR_COUNT) {
        *alarm_active = 0;
        LOG_INFO("alcohol alarm cleared");
    }

    sample->alcohol_alarm = *alarm_active;
}

static void update_motion_alarm(data_t *sample,
                                int *alarm_active,
                                int *high_count,
                                int *clear_count)
{
    if (sample->accel_valid && sample->motion_state >= MOTION_STATE_VIBRATION) {
        if (*high_count < MOTION_ALARM_ASSERT_COUNT) {
            ++(*high_count);
        }
        *clear_count = 0;
    } else {
        if (*clear_count < MOTION_ALARM_CLEAR_COUNT) {
            ++(*clear_count);
        }
        *high_count = 0;
    }

    if (*alarm_active == 0 && *high_count >= MOTION_ALARM_ASSERT_COUNT) {
        *alarm_active = 1;
        LOG_WARN("motion alarm asserted, state=%s x=%d y=%d z=%d",
                 motion_state_name((motion_state_t)sample->motion_state),
                 sample->accel_x,
                 sample->accel_y,
                 sample->accel_z);
    } else if (*alarm_active != 0 && *clear_count >= MOTION_ALARM_CLEAR_COUNT) {
        *alarm_active = 0;
        LOG_INFO("motion alarm cleared");
    }

    sample->motion_alarm = *alarm_active;
}

static void apply_beep_alarm(void *beep_ctx, int desired_on, int *beep_on)
{
    int target_state = desired_on ? BEEP_ON : BEEP_OFF;

    if (*beep_on == desired_on) {
        return;
    }

    if (beep_control_set(beep_ctx, target_state) == 0) {
        *beep_on = desired_on;
    } else if (desired_on) {
        LOG_ERROR("set beep on failed");
    } else {
        LOG_ERROR("set beep off failed");
    }
}

void *collect_thread(void *arg)
{
    sensor_registration_t registry[SENSOR_REGISTRY_CAPACITY];
    data_t sample;
    int mock_active = 0;
    void *mq3_ctx = NULL;
    void *adxl_ctx = NULL;
    void *dht11_ctx = NULL;
    void *beep_ctx = NULL;
    int mq3_online = 0;
    int adxl_online = 0;
    int dht11_online = 0;
    int alcohol_alarm_active = 0;
    int alcohol_high_count = 0;
    int alcohol_clear_count = 0;
    int motion_alarm_active = 0;
    int motion_high_count = 0;
    int motion_clear_count = 0;
    int beep_on = 0;
    int prev_accel_valid = 0;
    int prev_accel_x = 0;
    int prev_accel_y = 0;
    int prev_accel_z = 0;

    (void)arg;
    memset(registry, 0, sizeof(registry));

    srand((unsigned int)time(NULL));
    if (register_sensor(registry, SENSOR_REGISTRY_CAPACITY, &lm75_sensor_ops) != 0) {
        LOG_ERROR("register sensor failed: %s", lm75_sensor_ops.name);
    }

    if (mq3_sensor_init(&mq3_ctx) != 0 || mq3_ctx == NULL) {
        LOG_ERROR("mq3 sensor init failed");
    }

    if (adxl345_sensor_init(&adxl_ctx) != 0 || adxl_ctx == NULL) {
        LOG_ERROR("adxl345 sensor init failed");
    }

    if (dht11_sensor_init(&dht11_ctx) != 0 || dht11_ctx == NULL) {
        LOG_ERROR("dht11 sensor init failed");
    }

    if (beep_control_init(&beep_ctx) != 0 || beep_ctx == NULL) {
        LOG_ERROR("beep control init failed");
    }

    sleep(1);

    while (1) {
        memset(&sample, 0, sizeof(sample));
        sample.sample_time = time(NULL);

        fill_mock_channels(&sample);
        collect_humidity(dht11_ctx, &sample, &dht11_online);
        collect_temperature(registry, SENSOR_REGISTRY_CAPACITY, &sample, &mock_active);
        collect_alcohol(mq3_ctx, &sample, &mq3_online);
        collect_motion(adxl_ctx,
                       &sample,
                       &adxl_online,
                       &prev_accel_valid,
                       &prev_accel_x,
                       &prev_accel_y,
                       &prev_accel_z);
        collect_weather(&sample);
        update_alcohol_alarm(&sample,
                             &alcohol_alarm_active,
                             &alcohol_high_count,
                             &alcohol_clear_count);
        update_motion_alarm(&sample,
                            &motion_alarm_active,
                            &motion_high_count,
                            &motion_clear_count);
        apply_beep_alarm(beep_ctx,
                         alcohol_alarm_active || motion_alarm_active,
                         &beep_on);

        mailbox_send_msg("lvgl_thread", sample);
        mailbox_send_msg("mqtt_thread", sample);
        mailbox_send_msg("sqlite_thread", sample);

        sleep(COLLECT_POLL_INTERVAL_SEC);
    }

    unregister_sensors(registry, SENSOR_REGISTRY_CAPACITY);
    mq3_sensor_deinit(mq3_ctx);
    adxl345_sensor_deinit(adxl_ctx);
    dht11_sensor_deinit(dht11_ctx);
    beep_control_deinit(beep_ctx);
    return NULL;
}
