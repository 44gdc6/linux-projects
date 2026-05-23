MODULE_NAME := beep_drv
KERNEL_DIR ?= /home/linux/imx6ull/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
ROOTFS_DIR ?= ~/nfs/rootfs

obj-m += $(MODULE_NAME).o

.PHONY: all clean

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR) modules
	cp $(MODULE_NAME).ko $(ROOTFS_DIR)

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR) clean
