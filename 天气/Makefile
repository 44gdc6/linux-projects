OBJ:=./a.out                                                                

OBJS+=main.c
OBJS+=cJSON.c

$(OBJ):$(OBJS)
	gcc $^ -o $@ 
.PHONY:
clean:
	rm $(OBJ)
memcheck:
	valgrind --tool=memcheck --leak-check=full ./$(OBJ)
