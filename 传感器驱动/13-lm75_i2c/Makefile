#目标名
TARGET :=lm75

#驱动目录
DRV_TARGET_DIR := $(TARGET)_drv

#应用目录
APP_TARGET_DIR := $(TARGET)_app

#导出变量供子目录Makefile使用
export DRV_TARGET_DIR APP_TARGET_DIR

all:
	make -C $(DRV_TARGET_DIR)
	make -C $(APP_TARGET_DIR)

.PHONY:
clean:
	make -C $(DRV_TARGET_DIR) clean
	make -C $(APP_TARGET_DIR) clean
distclean:
	make -C $(DRV_TARGET_DIR) distclean
	make -C $(APP_TARGET_DIR) distclean