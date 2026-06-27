#include "service/geofence.h"
#include <math.h>

#define EARTH_RADIUS_METERS 6371000.0
#define DEG_TO_RAD(x) ((x) * M_PI / 180.0)

void geofence_init(geofence_config_t *cfg,
                   double center_lat,
                   double center_lon,
                   double radius_meters,
                   uint8_t enabled)
{
    if (cfg == NULL) {
        return;
    }
    cfg->center_lat = center_lat;
    cfg->center_lon = center_lon;
    cfg->radius_meters = radius_meters;
    cfg->enabled = enabled;
}

double geofence_distance_meters(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = DEG_TO_RAD(lat2 - lat1);
    double dlon = DEG_TO_RAD(lon2 - lon1);
    double a, c;

    double rad_lat1 = DEG_TO_RAD(lat1);
    double rad_lat2 = DEG_TO_RAD(lat2);

    a = sin(dlat / 2.0) * sin(dlat / 2.0) +
        cos(rad_lat1) * cos(rad_lat2) *
        sin(dlon / 2.0) * sin(dlon / 2.0);
    c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

    return EARTH_RADIUS_METERS * c;
}

int geofence_check(const geofence_config_t *cfg, double lat, double lon)
{
    double dist;

    if (cfg == NULL || !cfg->enabled) {
        return 0;
    }

    dist = geofence_distance_meters(cfg->center_lat, cfg->center_lon, lat, lon);
    return (dist > cfg->radius_meters) ? 1 : 0;
}
