#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <math.h>

// GPS数据结构定义
typedef struct {
    double latitude;      // 纬度（度）
    double longitude;     // 经度（度）
    char lat_dir;         // 纬度方向 N/S
    char lon_dir;         // 经度方向 E/W
    int hour;             // 小时（UTC）
    int minute;           // 分钟
    int second;           // 秒
    int day;              // 日
    int month;            // 月
    int year;             // 年
    char status;          // 定位状态 A=有效 V=无效
    int satellite_num;    // 卫星数量
    double altitude;      // 海拔高度（米）
    double speed;         // 地面速度（节）
    double course;        // 地面航向（度）
    int valid;            // 数据有效性标志
} GPS_INFO;

// 全局变量
static GPS_INFO gps_info;
static int gps_fd = -1;

// 函数声明
int open_serial_port(const char *port, int baudrate);
int configure_serial_port(int fd, int baudrate);
int read_gps_data(int fd, char *buffer, int size);
void parse_gps_data(const char *data);
int find_comma(const char *str, int index);
double convert_to_degree(double value);
void print_gps_info(void);

// 打开串口设备
int open_serial_port(const char *port, int baudrate) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("打开串口失败");
        return -1;
    }
    
    // 恢复串口为阻塞状态
    if (fcntl(fd, F_SETFL, 0) < 0) {
        perror("设置串口阻塞模式失败");
        close(fd);
        return -1;
    }
    
    // 配置串口参数
    if (configure_serial_port(fd, baudrate) < 0) {
        close(fd);
        return -1;
    }
    
    printf("串口 %s 打开成功，波特率：%d\n", port, baudrate);
    return fd;
}

// 配置串口参数
int configure_serial_port(int fd, int baudrate) {
    struct termios options;
    
    // 获取当前串口配置
    if (tcgetattr(fd, &options) < 0) {
        perror("获取串口属性失败");
        return -1;
    }
    
    // 设置输入输出波特率
    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);
    
    // 设置数据位：8位
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    
    // 设置停止位：1位
    options.c_cflag &= ~CSTOPB;
    
    // 设置校验位：无校验
    options.c_cflag &= ~PARENB;
    
    // 关闭硬件流控
    options.c_cflag &= ~CRTSCTS;
    
    // 启用接收器
    options.c_cflag |= CREAD | CLOCAL;
    
    // 设置输入模式：原始模式
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    
    // 设置输出模式：原始输出
    options.c_oflag &= ~OPOST;
    
    // 设置输入参数：关闭软件流控
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    
    // 设置特殊字符
    options.c_cc[VMIN] = 1;     // 最小读取字符数
    options.c_cc[VTIME] = 10;   // 超时时间（0.1秒单位）
    
    // 清空输入输出缓冲区
    tcflush(fd, TCIFLUSH);
    
    // 应用新的串口配置
    if (tcsetattr(fd, TCSANOW, &options) < 0) {
        perror("设置串口属性失败");
        return -1;
    }
    
    return 0;
}

// 读取GPS数据
int read_gps_data(int fd, char *buffer, int size) {
    fd_set readfds;
    struct timeval timeout;
    int ret;
    
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    
    // 设置超时时间为1秒
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    // 使用select等待数据可读
    ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
    
    if (ret < 0) {
        perror("select错误");
        return -1;
    } else if (ret == 0) {
        // 超时
        return 0;
    }
    
    // 读取数据
    if (FD_ISSET(fd, &readfds)) {
        ret = read(fd, buffer, size - 1);
        if (ret > 0) {
            buffer[ret] = '\0'; 
            printf("====================== 数据 =======================\n");
            printf("%s\n", buffer);
            printf("==================================================\n");
            return ret;
        } else if (ret == 0) {
            return 0;
        } else {
            perror("读取数据失败");
            return -1;
        }
    }
    
    return 0;
}

// 查找第n个逗号的位置
int find_comma(const char *str, int index) {
    int i, count = 0;
    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] == ',') {
            count++;
            if (count == index) {
                return i + 1; // 返回逗号后的位置
            }
        }
    }
    return -1;
}

// 将度分格式转换为度
double convert_to_degree(double value) {
    int degrees = (int)(value / 100);
    double minutes = value - degrees * 100;
    return degrees + minutes / 60.0;
}

// 解析GPS数据
void parse_gps_data(const char *data) {
    char *ptr;
    char temp[256];
    int pos;
    
    // 初始化GPS信息
    memset(&gps_info, 0, sizeof(GPS_INFO));
    gps_info.valid = 0;
    
    // 查找GNRMC或GPRMC语句（推荐最小定位数据）
    ptr = strstr(data, "$GNRMC");
    if (ptr == NULL) {
        ptr = strstr(data, "$GPRMC");
    }
    
    if (ptr != NULL) {
        strncpy(temp, ptr, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        
        // 解析UTC时间 HHMMSS.sss
        pos = find_comma(temp, 1);
        if (pos > 0 && strlen(temp) > pos + 6) {
            sscanf(temp + pos, "%2d%2d%2d", 
                   &gps_info.hour, &gps_info.minute, &gps_info.second);
        }
        
        // 解析定位状态
        pos = find_comma(temp, 2);
        if (pos > 0) {
            gps_info.status = temp[pos];
        }
        
        // 解析纬度
        pos = find_comma(temp, 3);
        if (pos > 0) {
            double lat_value;
            sscanf(temp + pos, "%lf", &lat_value);
            gps_info.latitude = convert_to_degree(lat_value);
        }
        
        // 解析纬度方向
        pos = find_comma(temp, 4);
        if (pos > 0) {
            gps_info.lat_dir = temp[pos];
        }
        
        // 解析经度
        pos = find_comma(temp, 5);
        if (pos > 0) {
            double lon_value;
            sscanf(temp + pos, "%lf", &lon_value);
            gps_info.longitude = convert_to_degree(lon_value);
        }
        
        // 解析经度方向
        pos = find_comma(temp, 6);
        if (pos > 0) {
            gps_info.lon_dir = temp[pos];
        }
        
        // 解析地面速度（节）
        pos = find_comma(temp, 7);
        if (pos > 0) {
            sscanf(temp + pos, "%lf", &gps_info.speed);
        }
        
        // 解析地面航向
        pos = find_comma(temp, 8);
        if (pos > 0) {
            sscanf(temp + pos, "%lf", &gps_info.course);
        }
        
        // 解析UTC日期 DDMMYY
        pos = find_comma(temp, 9);
        if (pos > 0 && strlen(temp) > pos + 5) {
            sscanf(temp + pos, "%2d%2d%2d", 
                   &gps_info.day, &gps_info.month, &gps_info.year);
            gps_info.year += 2000; // 转换为完整年份
        }
        
        gps_info.valid = (gps_info.status == 'A') ? 1 : 0;
    }
    
    // 查找GPGGA语句（全球定位系统定位数据）
    ptr = strstr(data, "$GPGGA");
    if (ptr == NULL) {
        ptr = strstr(data, "$GNGGA");
    }
    
    if (ptr != NULL) {
        strncpy(temp, ptr, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        
        // 解析UTC时间
        pos = find_comma(temp, 1);
        if (pos > 0 && strlen(temp) > pos + 6 && gps_info.hour == 0) {
            sscanf(temp + pos, "%2d%2d%2d", 
                   &gps_info.hour, &gps_info.minute, &gps_info.second);
        }
        
        // 解析纬度
        pos = find_comma(temp, 2);
        if (pos > 0 && gps_info.latitude == 0) {
            double lat_value;
            sscanf(temp + pos, "%lf", &lat_value);
            gps_info.latitude = convert_to_degree(lat_value);
        }
        
        // 解析纬度方向
        pos = find_comma(temp, 3);
        if (pos > 0 && gps_info.lat_dir == 0) {
            gps_info.lat_dir = temp[pos];
        }
        
        // 解析经度
        pos = find_comma(temp, 4);
        if (pos > 0 && gps_info.longitude == 0) {
            double lon_value;
            sscanf(temp + pos, "%lf", &lon_value);
            gps_info.longitude = convert_to_degree(lon_value);
        }
        
        // 解析经度方向
        pos = find_comma(temp, 5);
        if (pos > 0 && gps_info.lon_dir == 0) {
            gps_info.lon_dir = temp[pos];
        }
        
        // 解析定位质量指示
        pos = find_comma(temp, 6);
        if (pos > 0) {
            int fix_quality;
            sscanf(temp + pos, "%d", &fix_quality);
            if (gps_info.status == 0) {
                gps_info.status = (fix_quality > 0) ? 'A' : 'V';
                gps_info.valid = (fix_quality > 0) ? 1 : 0;
            }
        }
        
        // 解析卫星数量
        pos = find_comma(temp, 7);
        if (pos > 0) {
            sscanf(temp + pos, "%d", &gps_info.satellite_num);
        }
        
        // 解析海拔高度
        pos = find_comma(temp, 9);
        if (pos > 0) {
            sscanf(temp + pos, "%lf", &gps_info.altitude);
        }
    }
}

// 打印GPS信息
void print_gps_info(void) {
    printf("\n========== GPS信息 ==========\n");
    
    if (gps_info.valid) {
        printf("定位状态: 有效 (A)\n");
        
        // 显示北京时间（UTC+8）
        int beijing_hour = (gps_info.hour + 8) % 24;
        printf("北京时间: %02d:%02d:%02d\n", 
               beijing_hour, gps_info.minute, gps_info.second);
        printf("UTC时间: %02d:%02d:%02d\n", 
               gps_info.hour, gps_info.minute, gps_info.second);
        printf("日期: %04d-%02d-%02d\n", 
               gps_info.year, gps_info.month, gps_info.day);
        
        printf("纬度: %.6f° %c\n", gps_info.latitude, gps_info.lat_dir);
        printf("经度: %.6f° %c\n", gps_info.longitude, gps_info.lon_dir);
        
        // 转换为十进制格式（用于地图搜索）
        double lat_decimal = gps_info.latitude;
        double lon_decimal = gps_info.longitude;
        if (gps_info.lat_dir == 'S') lat_decimal = -lat_decimal;
        if (gps_info.lon_dir == 'W') lon_decimal = -lon_decimal;
        
        printf("纬度(十进制): %.6f\n", lat_decimal);
        printf("经度(十进制): %.6f\n", lon_decimal);
        
        printf("卫星数量: %d\n", gps_info.satellite_num);
        printf("海拔高度: %.1f 米\n", gps_info.altitude);
        
        // 速度转换：1节 = 1.852公里/小时
        double speed_kmh = gps_info.speed * 1.852;
        printf("地面速度: %.2f 节 (%.2f km/h)\n", gps_info.speed, speed_kmh);
        printf("地面航向: %.1f°\n", gps_info.course);
        
        // 显示定位质量
        if (gps_info.satellite_num >= 4) {
            printf("定位质量: 良好\n");
        } else if (gps_info.satellite_num >= 1) {
            printf("定位质量: 一般\n");
        } else {
            printf("定位质量: 差\n");
        }
    } else {
        printf("定位状态: 无效 (V)\n");
        printf("提示: 请确保GPS模块在室外空旷区域，天线朝上无遮挡\n");
        printf("卫星数量: %d\n", gps_info.satellite_num);
        
        if (gps_info.satellite_num > 0) {
            printf("正在搜索卫星... 已找到 %d 颗卫星\n", gps_info.satellite_num);
        } else {
            printf("未搜索到卫星，请检查连接和天线方向\n");
        }
    }
    
    printf("=============================\n");
}

// 主函数
int main(int argc, char *argv[]) {
    const char *serial_port = "/dev/ttymxc2";
    int baudrate = 9600;
    char buffer[1024];
    int bytes_read;
    int line_count = 0;
    
    printf("IMX6ULL GPS数据读取程序\n");
    printf("串口设备: %s\n", serial_port);
    printf("波特率: %d\n", baudrate);
    printf("GPS模块: ATGM336H\n");
    printf("启动时间: %s %s\n", __DATE__, __TIME__);
    printf("\n等待GPS数据...\n");
    
    // 打开串口
    gps_fd = open_serial_port(serial_port, baudrate);
    if (gps_fd < 0) {
        fprintf(stderr, "无法打开串口设备\n");
        return EXIT_FAILURE;
    }
    
    // 主循环
    while (1) {
        // 读取GPS数据
        bytes_read = read_gps_data(gps_fd, buffer, sizeof(buffer));
        
        if (bytes_read > 0) {
            // 解析GPS数据
            parse_gps_data(buffer);
            
            // 每10行数据打印一次信息
            line_count++;
            if (line_count % 10 == 0) {
                print_gps_info();
                
                // 保存有效数据到文件（可选）
                if (gps_info.valid) {
                    FILE *fp = fopen("gps_log.txt", "a");
                    if (fp) {
                        fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d, %.6f, %.6f, %.1f, %d\n",
                                gps_info.year, gps_info.month, gps_info.day,
                                gps_info.hour, gps_info.minute, gps_info.second,
                                gps_info.latitude, gps_info.longitude,
                                gps_info.altitude, gps_info.satellite_num);
                        fclose(fp);
                    }
                }
            }
            
            // 显示原始数据（可选，调试用）
            if (line_count % 50 == 0) {
                printf("\n--- 原始数据样例 ---\n");
                char *line = strtok(buffer, "\n");
                int i = 0;
                for (; i < 3 && line != NULL; i++) {
                    printf("%s\n", line);
                    line = strtok(NULL, "\n");
                }
                printf("-------------------\n");
            }
        } else if (bytes_read == 0) {
            // 超时，显示等待信息
            static int wait_count = 0;
            wait_count++;
            if (wait_count % 20 == 0) {
                printf(".");
                fflush(stdout);
            }
        }
        
        // 短暂休眠，减少CPU占用
        usleep(10000); // 10ms
    }
    
    // 关闭串口（实际上不会执行到这里）
    close(gps_fd);
    return EXIT_SUCCESS;
}
