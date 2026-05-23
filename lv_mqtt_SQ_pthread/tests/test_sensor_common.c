#include <assert.h>
#include <math.h>
#include <string.h>

#include "sensor_common.h"

static void test_lm75_raw_to_celsius(void)
{
    assert(fabs(lm75_raw_to_celsius(40) - 20.0) < 0.0001);
    assert(fabs(lm75_raw_to_celsius(0) - 0.0) < 0.0001);
}

static void test_temp_source_name(void)
{
    assert(strcmp(temp_source_name(TEMP_SOURCE_LM75), "lm75") == 0);
    assert(strcmp(temp_source_name(TEMP_SOURCE_MOCK), "mock") == 0);
}

int main(void)
{
    test_lm75_raw_to_celsius();
    test_temp_source_name();
    return 0;
}
