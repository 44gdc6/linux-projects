#目标文件名
OBJ := $(DRV_TARGET_DIR)

#内核路径
kerdir := /home/linux/imx6ull/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek

#当前驱动工程目录
curdir := $(shell pwd)

#代码添加到工程编译选项中
obj-m += $(OBJ).o 

all:
	make -C $(kerdir) M=$(curdir) modules
	cp $(OBJ).ko ~/nfs/rootfs  

.PHONY:
clean:
	make -C $(kerdir) M=$(curdir) modules clean
distclean:
	make -C $(kerdir) M=$(curdir) modules clean
	rm ~/nfs/rootfs/$(OBJ).ko