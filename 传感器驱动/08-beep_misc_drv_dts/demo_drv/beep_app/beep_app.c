#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#define BEEP_ON  1
#define BEEP_OFF 0

int main(void)
{
    int fd = 0;
    int stat = BEEP_OFF;
    int readstat = 0;

    fd = open("/dev/beep_misc", O_RDWR);
    if (-1 == fd)
    {
        perror("fail to open");
        return -1;
    }

    while (1)
    {
        stat = BEEP_ON;
        write(fd, &stat, 4);
        read(fd, &readstat, 4);
        printf("current light stat:%s\n", readstat == BEEP_ON ? "BEEP_ON" : "BEEP_OFF");
        sleep(1);

        stat = BEEP_OFF;
        write(fd, &stat, 4);
        read(fd, &readstat, 4);
        printf("current light stat:%s\n", readstat == BEEP_ON ? "BEEP_ON" : "BEEP_OFF");
        sleep(1);
    }
    
    close(fd);

    return 0;
}