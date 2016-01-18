APP = vhost_net_app 
C_SRCS = client.c main.c sh_mem.c util.c uvhost.c virt_queue.c


C_OBJS = ${C_SRCS:.c=.o}

program_OBJS = $(C_OBJS)


CFLAGS =  -std=c99 -Wc11-extensions -Wall -pedantic -pedantic-errors -g -lrt -D_POSIX_C_SOURCE=200112L 
 
.PHONY: all clean distclean 

all: $(APP)

$(APP): $(program_OBJS)
		$(CC) $(CFLAGS) $(program_OBJS) -o $(APP)


clean:
		rm -f *.o *.core $(APP)	

distclean: clean
