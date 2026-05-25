#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    int fd = 0;
    short tmpdata = 0;
    double tempval = 0;

    fd = open("/dev/lm75_misc", O_RDWR);
    if (-1 == fd) {
        perror("fail to open");
        return -1;
    }

    while (1)
    {
        read(fd, &tmpdata, sizeof(tmpdata));
        tempval = tmpdata * 0.5;
        printf("TEMP:%.2lf\n", tempval);
        sleep(1);
    }

    close(fd);

    return 0;
}