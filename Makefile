obj-m+=ledmsgchar.o

all: modules test

modules:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules

test:   testledmsgchar.c
	$(CC) testledmsgchar.c -o test

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
