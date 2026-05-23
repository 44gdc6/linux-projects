#include "head.h"

int CreateTcpConnection(const char *pip, int port)
{
    int ret = 0;
    int sockfd = 0;
    struct sockaddr_in seraddr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd)
    {
        perror("fail to socket");
        return -1;
    }

    seraddr.sin_family = AF_INET;
    seraddr.sin_port = htons(port);
    seraddr.sin_addr.s_addr = inet_addr(pip);
    ret = connect(sockfd, (struct sockaddr *)&seraddr, sizeof(seraddr));
    if (-1 == ret)
    {
        perror("fail to connect");
        return -1;
    }

    return sockfd;
}

int SendHttpRequest(int sockfd, const char *purl)
{
    char sendbuf[4096] = {0};
    ssize_t nret = 0;

    sprintf(sendbuf, "GET %s HTTP/1.1\r\n", purl);
    sprintf(sendbuf, "%sHost: api.k780.com\r\n", sendbuf);
    sprintf(sendbuf, "%sUser-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:109.0) Gecko/20100101 Firefox/113.0\r\n", sendbuf);
    sprintf(sendbuf, "%sAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8\r\n", sendbuf);
    sprintf(sendbuf, "%sAccept-Language: en-US,en;q=0.5\r\n", sendbuf);
    sprintf(sendbuf, "%sAccept-Encoding: gzip, deflate\r\n", sendbuf);
    sprintf(sendbuf, "%sConnection: keep-alive\r\n", sendbuf);
    sprintf(sendbuf, "%sUpgrade-Insecure-Requests: 1\r\n", sendbuf);
    sprintf(sendbuf, "%s\r\n", sendbuf);
    
    nret = send(sockfd, sendbuf, strlen(sendbuf), 0);
    if (-1 == nret)
    {
        perror("fail to send");
        return -1;
    }

    return 0;
}

int RecvHttpRespone(int sockfd, char *ptmpbuff, int maxlen)
{
    ssize_t nret = 0;
    nret = recv(sockfd, ptmpbuff, maxlen, 0);
    if (-1 == nret)
    {
        perror("fail to recv");
        return -1;
    }

    // 找到HTTP响应的正文部分（跳过HTTP头部）
    char *body_start = strstr(ptmpbuff, "\r\n\r\n");
    if (body_start == NULL) {
        printf("Invalid HTTP response\n");
        return -1;
    }
    body_start += 4;  // 跳过 "\r\n\r\n"

    // 如果是chunked编码，需要解析chunked
    if (strstr(ptmpbuff, "Transfer-Encoding: chunked") != NULL) {
        char *json_start = NULL;
        char *chunk_ptr = body_start;
        
        // 找到第一个chunk的大小行
        char *chunk_size_end = strstr(chunk_ptr, "\r\n");
        if (chunk_size_end) {
            // 跳过chunk大小行
            chunk_ptr = chunk_size_end + 2;
            
            // 现在chunk_ptr指向第一个chunk的数据开始位置
            // chunk数据后面会有\r\n0\r\n\r\n
            char *chunk_end = strstr(chunk_ptr, "\r\n0\r\n\r\n");
            if (chunk_end) {
                json_start = chunk_ptr;
                *chunk_end = '\0';  // 在JSON数据末尾添加结束符
            } else {
                printf("Failed to find chunk end\n");
                return -1;
            }
        } else {
            printf("Invalid chunked encoding\n");
            return -1;
        }
        
        if (json_start) {
            // 解析JSON
            cJSON *json = cJSON_Parse(json_start);
            if (json != NULL) {
                cJSON *success = cJSON_GetObjectItem(json, "success");
                if (success && strcmp(success->valuestring, "1") == 0) {
                    cJSON *result = cJSON_GetObjectItem(json, "result");
                    if (result && cJSON_IsArray(result)) {
                        int array_size = cJSON_GetArraySize(result);
                        for (int i = 0; i < array_size; i++) {
                                cJSON *item = cJSON_GetArrayItem(result, i);
                                if (item) {
                                    cJSON *days = cJSON_GetObjectItem(item, "days");
                                    cJSON *week = cJSON_GetObjectItem(item, "week");
                                    cJSON *citynm = cJSON_GetObjectItem(item, "citynm");
                                    cJSON *weather = cJSON_GetObjectItem(item, "weather");
                                    cJSON *temperature = cJSON_GetObjectItem(item, "temperature");
                                    cJSON *wind = cJSON_GetObjectItem(item, "wind");
                                    
                                    if (days && week && citynm && weather && temperature && wind) {
                                        printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\n", 
                                               days->valuestring,
                                               week->valuestring,
                                               citynm->valuestring,
                                               weather->valuestring,
                                               temperature->valuestring,
                                               weather->valuestring,
                                               wind->valuestring);
                                    }
                                }
                            }
                        }
                    cJSON_Delete(json);
                } else {
                    printf("API request failed\n");
                    if (json) cJSON_Delete(json);
                }
            } else {
                printf("Failed to parse JSON\n");
            }
        }
    }

    return 0;
}


int main(void)
{
    int sockfd = 0;
    char tmpbuff[4096] = {0};
    char CityName[256] = {0};
    char Retbuff[4096] = {0};
    gets(CityName);
    sockfd = CreateTcpConnection("103.205.5.206", 80);
    sprintf(Retbuff,"/?app=weather.future&weaid=%s&appkey=10003&sign=b59bc3ef6191eb9f747dd4e83c99f2a4&format=json",CityName);
    SendHttpRequest(sockfd, Retbuff);
    RecvHttpRespone(sockfd, tmpbuff, 4096);
    close(sockfd);

    return 0;
}