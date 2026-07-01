#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

// 和驱动里一模一样的结构体
struct mpu6050_data {
    short accel_x;
    short accel_y;
    short accel_z;
    short temp;
    short gyro_x;
    short gyro_y;
    short gyro_z;
};

int main(void)
{
    int fd;
    struct mpu6050_data data;
    int ret;

    // ① 打开设备
    fd = open("/dev/mpu6050_misc", O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        return -1;
    }

    // ② 循环读取（Ctrl+C 退出）
    while (1) {
        ret = read(fd, &data, sizeof(data));
        if (ret != sizeof(data)) {
            perror("read failed");
            break;
        }

        printf("ACC: %+6d %+6d %+6d | ",
               data.accel_x, data.accel_y, data.accel_z);
        printf("GYR: %+6d %+6d %+6d | ",
               data.gyro_x, data.gyro_y, data.gyro_z);
        printf("TEMP: %d\n", data.temp);

        sleep(1);
    }

    // ③ 关闭
    close(fd);
    return 0;
}

