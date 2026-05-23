#include "log.h"
#include <time.h>

/* 日志文件句柄 */
FILE *fLog = NULL;

/* 当前日志级别 */
LogLevel_t gCurLogLevel = LOG_LEVEL_TRACE;

//打开日志文件
int LogInit(const char *pLogPath)
{
    fLog = fopen(pLogPath, "a");
    if (NULL == fLog)
    {
        perror("fail to fopen");
        return -1;
    }

    return 0;
}

//关闭日志文件
int LogDeInit(void)
{
    if (fLog != NULL)
    {
        fclose(fLog);
        fLog = NULL;
    }

    return 0;
}

//设置当前日志级别
void LogSetLevel(LogLevel_t level)
{
    gCurLogLevel = level;

    return;
}

//向日志文件中写入消息
int LogWrite(LogLevel_t level, char *pmsg)
{
    time_t t;
    struct tm *ptm = NULL;

    if (level > gCurLogLevel)
    {
        return 0;
    }

    time(&t);
    ptm = localtime(&t);
    fprintf(fLog, "[%04d-%02d-%02d %02d:%02d:%02d][%s:%d]:%s\n", ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday, 
                                                          ptm->tm_hour, ptm->tm_min, ptm->tm_sec, 
                                                          __FILE__, __LINE__, pmsg);

    return 0;
}