#ifndef __GEOFENCE_H__
#define __GEOFENCE_H__

#include <stdint.h>

typedef struct {
    double center_lat;      /* 围栏中心纬度 (十进制度) */
    double center_lon;      /* 围栏中心经度 (十进制度) */
    double radius_meters;   /* 围栏半径 (米) */
    uint8_t enabled;        /* 1=启用围栏检测 */
} geofence_config_t;

/* 初始化围栏配置 */
void geofence_init(geofence_config_t *cfg,
                   double center_lat,
                   double center_lon,
                   double radius_meters,
                   uint8_t enabled);

/* 检查是否越界: 返回 1=越界, 0=安全 */
int geofence_check(const geofence_config_t *cfg, double lat, double lon);

/* 计算两点间距离 (米) */
double geofence_distance_meters(double lat1, double lon1, double lat2, double lon2);

#endif
