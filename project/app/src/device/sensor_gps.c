#include "device/sensor_gps.h"
#include "config/app_config.h"
#include "core/log.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

static int g_gps_fd = -1;

static double convert_to_degree(double value)
{
    int degrees = (int)(value / 100);
    double minutes = value - degrees * 100;
    return degrees + minutes / 60.0;
}

static int find_comma(const char *str, int index)
{
    int i, count = 0;
    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] == ',') {
            count++;
            if (count == index) {
                return i + 1;
            }
        }
    }
    return -1;
}

static void parse_rmc(const char *sentence, gps_info_t *info)
{
    char temp[256];
    int pos;

    strncpy(temp, sentence, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    /* 字段1: UTC时间 HHMMSS.sss */
    pos = find_comma(temp, 1);
    if (pos > 0 && strlen(temp + pos) >= 6) {
        sscanf(temp + pos, "%2hhu%2hhu%2hhu",
               &info->hour, &info->minute, &info->second);
    }

    /* 字段2: 定位状态 A=有效 V=无效 */
    pos = find_comma(temp, 2);
    if (pos > 0) {
        info->valid = (temp[pos] == 'A') ? 1 : 0;
    }

    /* 字段3: 纬度 ddmm.mmmm */
    pos = find_comma(temp, 3);
    if (pos > 0) {
        double lat_raw;
        sscanf(temp + pos, "%lf", &lat_raw);
        info->latitude = convert_to_degree(lat_raw);
    }

    /* 字段4: 纬度方向 N/S */
    pos = find_comma(temp, 4);
    if (pos > 0 && temp[pos] == 'S') {
        info->latitude = -info->latitude;
    }

    /* 字段5: 经度 dddmm.mmmm */
    pos = find_comma(temp, 5);
    if (pos > 0) {
        double lon_raw;
        sscanf(temp + pos, "%lf", &lon_raw);
        info->longitude = convert_to_degree(lon_raw);
    }

    /* 字段6: 经度方向 E/W */
    pos = find_comma(temp, 6);
    if (pos > 0 && temp[pos] == 'W') {
        info->longitude = -info->longitude;
    }

    /* 字段7: 地面速度 (节) */
    pos = find_comma(temp, 7);
    if (pos > 0) {
        float speed_knots;
        sscanf(temp + pos, "%f", &speed_knots);
        info->speed = speed_knots * 1.852f;  /* 节 → km/h */
    }

    /* 字段8: 地面航向 (度) */
    pos = find_comma(temp, 8);
    if (pos > 0) {
        sscanf(temp + pos, "%f", &info->course);
    }
}

static void parse_gga(const char *sentence, gps_info_t *info)
{
    char temp[256];
    int pos;

    strncpy(temp, sentence, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    /* 字段7: 卫星数 */
    pos = find_comma(temp, 7);
    if (pos > 0) {
        int sat;
        sscanf(temp + pos, "%d", &sat);
        info->satellites = (uint8_t)sat;
    }

    /* 字段9: 海拔 (米) */
    pos = find_comma(temp, 9);
    if (pos > 0) {
        sscanf(temp + pos, "%f", &info->altitude);
    }

    /* 如果 RMC 没有设置定位状态，用 GGA 的定位质量补充 */
    if (info->valid == 0) {
        pos = find_comma(temp, 6);
        if (pos > 0) {
            int fix_quality;
            sscanf(temp + pos, "%d", &fix_quality);
            if (fix_quality > 0) {
                info->valid = 1;
            }
        }
    }
}

int gps_init(const char *uart_path)
{
    struct termios tty;

    g_gps_fd = open(uart_path, O_RDONLY | O_NOCTTY);
    if (g_gps_fd < 0) {
        LOG_ERROR("gps: open %s failed", uart_path);
        return -1;
    }

    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(g_gps_fd, &tty) < 0) {
        LOG_ERROR("gps: tcgetattr failed");
        close(g_gps_fd);
        g_gps_fd = -1;
        return -1;
    }

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;           /* 8 data bits */
    tty.c_cflag &= ~CSTOPB;       /* 1 stop bit */
    tty.c_cflag &= ~PARENB;       /* no parity */
    tty.c_cflag &= ~CRTSCTS;      /* no hw flow control */
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_oflag &= ~OPOST;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;         /* 1 second read timeout */

    tcflush(g_gps_fd, TCIFLUSH);
    if (tcsetattr(g_gps_fd, TCSANOW, &tty) < 0) {
        LOG_ERROR("gps: tcsetattr failed");
        close(g_gps_fd);
        g_gps_fd = -1;
        return -1;
    }

    LOG_INFO("gps: initialized on %s (9600 8N1)", uart_path);
    return 0;
}

int gps_read(gps_info_t *info)
{
    char buf[512];
    fd_set rfds;
    struct timeval tv;
    ssize_t n;

    if (g_gps_fd < 0 || info == NULL) {
        return -1;
    }

    memset(info, 0, sizeof(*info));

    FD_ZERO(&rfds);
    FD_SET(g_gps_fd, &rfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    if (select(g_gps_fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
        return -1;
    }

    n = read(g_gps_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';

    /* 解析 $GPRMC / $GNRMC */
    char *rmc = strstr(buf, "$GNRMC");
    if (rmc == NULL) {
        rmc = strstr(buf, "$GPRMC");
    }
    if (rmc != NULL) {
        parse_rmc(rmc, info);
    }

    /* 解析 $GPGGA / $GNGGA (补充卫星数、海拔) */
    char *gga = strstr(buf, "$GNGGA");
    if (gga == NULL) {
        gga = strstr(buf, "$GPGGA");
    }
    if (gga != NULL) {
        parse_gga(gga, info);
    }

    return (info->valid) ? 0 : -1;
}

void gps_deinit(void)
{
    if (g_gps_fd >= 0) {
        close(g_gps_fd);
        g_gps_fd = -1;
    }
}
