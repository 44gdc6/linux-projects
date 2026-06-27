#ifndef __SENSOR_GPS_H__
#define __SENSOR_GPS_H__

#include <stdint.h>

typedef struct {
    double latitude;       /* 纬度 (十进制度, 北纬正, 南纬负) */
    double longitude;      /* 经度 (十进制度, 东经正, 西经负) */
    float altitude;        /* 海拔 (米) */
    float speed;           /* 地面速度 (km/h) */
    float course;          /* 航向 (度) */
    uint8_t satellites;    /* 卫星数 */
    uint8_t valid;         /* 1=定位有效, 0=无效 */
    uint8_t hour;          /* UTC 小时 */
    uint8_t minute;        /* UTC 分钟 */
    uint8_t second;        /* UTC 秒 */
} gps_info_t;

int gps_init(const char *uart_path);
int gps_read(gps_info_t *info);
void gps_deinit(void);

#endif
