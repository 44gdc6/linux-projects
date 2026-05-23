CC = arm-linux-gnueabihf-gcc
APP_DIR := $(CURDIR)
BUILD_DIR := $(APP_DIR)/build
THIRD_PARTY_DIR := $(APP_DIR)/third_party
LVGL_DIR_NAME ?= lvgl
LV_DRIVERS_DIR_NAME ?= lv_drivers
LVGL_DIR := $(THIRD_PARTY_DIR)

ifeq ($(shell which $(CC) 2>/dev/null),)
$(error cross compiler '$(CC)' not found in PATH)
endif

CFLAGS ?= -O3 -g0 -Wall -Wshadow -Wundef -Wmissing-prototypes -std=c99 -Wextra \
	-Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith -fno-strict-aliasing \
	-Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized -Wno-unused-parameter \
	-Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess \
	-Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default \
	-Wreturn-type -Wmultichar -Wformat-security -Wno-ignored-qualifiers \
	-Wno-error=pedantic -Wno-sign-compare -Wno-error=missing-prototypes \
	-Wdouble-promotion -Wclobbered -Wdeprecated -Wempty-body -Wstack-usage=2048
CFLAGS += -DLV_CONF_INCLUDE_SIMPLE -DLV_DRV_CONF_INCLUDE_SIMPLE

INCPATH += -I$(APP_DIR)/include -I$(APP_DIR)/ui -I$(THIRD_PARTY_DIR)
LDFLAGS ?= -lm -lpthread -lsqlite3 -lpaho-mqtt3c -lrt -lssl -lcrypto
LDPATH += -L$(APP_DIR)/../lib
BIN := $(BUILD_DIR)/demo
ROOTFS_DIR ?= ~/nfs/rootfs

include $(THIRD_PARTY_DIR)/lvgl/lvgl.mk
include $(THIRD_PARTY_DIR)/lv_drivers/lv_drivers.mk

APP_CSRCS += src/core/main.c
APP_CSRCS += src/core/linklist.c
APP_CSRCS += src/core/linkqueue.c
APP_CSRCS += src/core/mailbox.c
APP_CSRCS += src/core/log.c
APP_CSRCS += src/device/sensor_common.c
APP_CSRCS += src/device/cJSON.c
APP_CSRCS += src/device/sensor_lm75.c
APP_CSRCS += src/device/sensor_mq3.c
APP_CSRCS += src/device/sensor_adxl345.c
APP_CSRCS += src/device/sensor_dht11.c
APP_CSRCS += src/device/weather_client.c
APP_CSRCS += src/device/beep_control.c
APP_CSRCS += src/service/collect.c
APP_CSRCS += src/service/weather.c
APP_CSRCS += src/comm/mqtt.c
APP_CSRCS += src/comm/can_node.c
APP_CSRCS += src/storage/sqlite.c
APP_CSRCS += src/ui/lvgl_user.c
APP_CSRCS += ui/mouse_cursor_icon.c
APP_CSRCS += ui/ui.c
APP_CSRCS += ui/ui_events.c
APP_CSRCS += ui/ui_helpers.c
APP_CSRCS += ui/screens/ui_Screen1.c
APP_CSRCS += ui/fonts/ui_font_Font1.c

REL_TP_CSRCS := $(filter-out /%,$(filter-out $(APP_DIR)/%,$(CSRCS)))
ABS_TP_CSRCS := $(filter $(APP_DIR)/%,$(CSRCS))

APP_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_CSRCS))
REL_TP_OBJS := $(patsubst %.c,$(BUILD_DIR)/third_party/lvgl/%.o,$(REL_TP_CSRCS))
ABS_TP_OBJS := $(patsubst $(APP_DIR)/%.c,$(BUILD_DIR)/%.o,$(ABS_TP_CSRCS))
OBJS := $(APP_OBJS) $(REL_TP_OBJS) $(ABS_TP_OBJS)

TEST_BIN := $(BUILD_DIR)/tests/test_sensor_common
TEST_LOG_BIN := $(BUILD_DIR)/tests/test_log
TEST_WEATHER_BIN := $(BUILD_DIR)/tests/test_weather_client

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDPATH)
	cp $(BIN) $(ROOTFS_DIR)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCPATH) -c $< -o $@

$(BUILD_DIR)/third_party/lvgl/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCPATH) -c $< -o $@

test: $(TEST_BIN) $(TEST_LOG_BIN) $(TEST_WEATHER_BIN)
	$(TEST_BIN)
	$(TEST_LOG_BIN)
	$(TEST_WEATHER_BIN)

$(TEST_BIN): $(APP_DIR)/tests/test_sensor_common.c $(APP_DIR)/src/device/sensor_common.c
	@mkdir -p $(BUILD_DIR)/tests
	$(CC) -I$(APP_DIR)/include $(APP_DIR)/tests/test_sensor_common.c $(APP_DIR)/src/device/sensor_common.c -o $@

$(TEST_LOG_BIN): $(APP_DIR)/tests/test_log.c $(APP_DIR)/src/core/log.c
	@mkdir -p $(BUILD_DIR)/tests
	$(CC) -I$(APP_DIR)/include $(APP_DIR)/tests/test_log.c $(APP_DIR)/src/core/log.c -lpthread -o $@

$(TEST_WEATHER_BIN): $(APP_DIR)/tests/test_weather_client.c $(APP_DIR)/src/device/weather_client.c $(APP_DIR)/src/device/cJSON.c
	@mkdir -p $(BUILD_DIR)/tests
	$(CC) -I$(APP_DIR)/include $(APP_DIR)/tests/test_weather_client.c $(APP_DIR)/src/device/weather_client.c $(APP_DIR)/src/device/cJSON.c -o $@

clean:
	rm -rf $(BUILD_DIR)
