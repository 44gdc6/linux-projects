#目标文件
OBJ:=$(APP_TARGET_DIR)

#源文件
OBJS+=$(OBJ).c

#工具链
CC:=arm-linux-gnueabihf-gcc

$(OBJ):$(OBJS)
	$(CC) $^ -o $@
	cp $(OBJ) ~/nfs/rootfs 

.PHONY:
clean:
	rm $(OBJ)
distclean:
	rm $(OBJ)
	rm ~/nfs/rootfs/$(OBJ)