#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    int fd = 0;
    short data[3] = {0};

    fd = open("/dev/adxl345_misc", O_RDWR);
    if (-1 == fd) {
        perror("fail to open");
        return -1;
    }

    while (1)
    {
        read(fd, data, sizeof(data));
        printf("x:%6d, y:%6d, z:%6d\r", data[0], data[1], data[2]);
        fflush(stdout);
        usleep(10000);
    }

    close(fd);

    return 0;
}