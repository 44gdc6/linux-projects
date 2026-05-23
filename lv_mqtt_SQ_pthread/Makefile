#
# Makefile
#
CC = arm-linux-gnueabihf-gcc
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= ${shell pwd}
CFLAGS ?= -O3 -g0 -I$(LVGL_DIR)/ -Wall -Wshadow -Wundef -Wmissing-prototypes -std=c99 -Wall -Wextra -Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith -fno-strict-aliasing -Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-ignored-qualifiers -Wno-error=pedantic -Wno-sign-compare -Wno-error=missing-prototypes -Wdouble-promotion -Wclobbered -Wdeprecated -Wempty-body -Wtype-limits -Wstack-usage=2048 -Wno-unused-value -Wno-unused-parameter -Wno-missing-field-initializers -Wuninitialized -Wmaybe-uninitialized -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wpointer-arith -Wno-cast-qual -Wmissing-prototypes -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-ignored-qualifiers -Wno-sign-compare
#-Wno-discarded-qualifiers 
#-Wshift-negative-value 
#-Wno-discarded-qualifiers 
LDFLAGS ?= -lm -lpthread -lsqlite3 -lpaho-mqtt3c -lrt -lssl -lcrypto
INCPATH += -I./include
LDPATH += -L./lib
BIN = demo


#Collect the files to compile
MAINSRC = ./main.c

include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

CSRCS +=$(LVGL_DIR)/mouse_cursor_icon.c 
CSRCS +=$(LVGL_DIR)/ui.c
CSRCS +=$(LVGL_DIR)/screens/ui_Screen1.c
CSRCS +=$(LVGL_DIR)/fonts/ui_font_Font1.c
CSRCS +=$(LVGL_DIR)/lvgl_user.c 
CSRCS +=$(LVGL_DIR)/linkqueue.c 
CSRCS +=$(LVGL_DIR)/linklist.c
CSRCS +=$(LVGL_DIR)/mailbox.c 
CSRCS +=$(LVGL_DIR)/sqlite.c
CSRCS +=$(LVGL_DIR)/mqtt.c
CSRCS +=$(LVGL_DIR)/collect.c
CSRCS +=$(LVGL_DIR)/sensor_common.c
CSRCS +=$(LVGL_DIR)/sensor_lm75.c
CSRCS +=$(LVGL_DIR)/ui_events.c
#CSRCS +=$(LVGL_DIR)/images/ui_img_icon1_png.c
#CSRCS +=$(LVGL_DIR)/images/ui_img_icon2_png.c

OBJEXT ?= .o

AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))

MAINOBJ = $(MAINSRC:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS) $(MAINSRC)
OBJS = $(AOBJS) $(COBJS)

## MAINOBJ -> OBJFILES

all: default

%.o: %.c
	@$(CC)  $(CFLAGS) -c $< -o $@ $(INCPATH)
	@echo "CC $<"
    
default: $(AOBJS) $(COBJS) $(MAINOBJ)
	$(CC) -o $(BIN) $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS) $(LDPATH)
	cp $(BIN) ~/nfs/rootfs
clean: 
	rm -f $(BIN) $(AOBJS) $(COBJS) $(MAINOBJ)
