#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "core/log.h"

int main(void)
{
    FILE *fp = NULL;
    char buffer[512];

    remove("test_app.log");

    assert(log_init("test_app.log") == 0);
    log_set_level(LOG_LEVEL_INFO);
    log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, "hello %s", "log");
    log_deinit();

    fp = fopen("test_app.log", "r");
    assert(fp != NULL);
    assert(fgets(buffer, sizeof(buffer), fp) != NULL);
    fclose(fp);

    assert(strstr(buffer, "[INFO]") != NULL);
    assert(strstr(buffer, "hello log") != NULL);

    remove("test_app.log");
    return 0;
}
