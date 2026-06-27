#include "service/gps_service.h"
#include "device/sensor_gps.h"
#include "service/geofence.h"
#include "config/app_config.h"
#include "core/log.h"
#include "core/mailbox.h"

#include <string.h>
#include <time.h>
#include <unistd.h>

void *gps_thread(void *arg)
{
    gps_info_t gps;
    geofence_config_t fence;
    int prev_fence_alarm = 0;

    (void)arg;

    geofence_init(&fence,
                  GEOFENCE_CENTER_LAT,
                  GEOFENCE_CENTER_LON,
                  GEOFENCE_RADIUS_METERS,
                  GEOFENCE_ENABLED);

    if (gps_init(GPS_UART_PATH) != 0) {
        LOG_ERROR("gps_thread: init failed on %s", GPS_UART_PATH);
        return NULL;
    }

    LOG_INFO("gps_thread started on %s", GPS_UART_PATH);

    while (1) {
        data_t sample;
        memset(&sample, 0, sizeof(sample));
        sample.sample_time = time(NULL);

        if (gps_read(&gps) == 0 && gps.valid) {
            sample.gps_latitude = gps.latitude;
            sample.gps_longitude = gps.longitude;
            sample.gps_altitude = gps.altitude;
            sample.gps_speed = gps.speed;
            sample.gps_course = gps.course;
            sample.gps_satellites = gps.satellites;
            sample.gps_valid = 1;

            /* 电子围栏检测 */
            sample.gps_fence_alarm = geofence_check(
                &fence, gps.latitude, gps.longitude);

            if (sample.gps_fence_alarm && !prev_fence_alarm) {
                LOG_WARN("GEO FENCE ALARM: vehicle outside safe zone! "
                         "lat=%.6f lon=%.6f dist>%.0fm",
                         gps.latitude, gps.longitude,
                         fence.radius_meters);
            }
            prev_fence_alarm = sample.gps_fence_alarm;

            LOG_DEBUG("gps: lat=%.6f lon=%.6f spd=%.1f sat=%d fence=%d",
                      gps.latitude, gps.longitude,
                      gps.speed, gps.satellites,
                      sample.gps_fence_alarm);
        }

        mailbox_send_msg("mqtt_thread", sample);
        mailbox_send_msg("lvgl_thread", sample);
        mailbox_send_msg("sqlite_thread", sample);

        sleep(GPS_POLL_INTERVAL_SEC);
    }

    gps_deinit();
    return NULL;
}
