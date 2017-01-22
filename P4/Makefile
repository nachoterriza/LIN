obj-m +=  mymodtimer.o
mymodtimer-objs += cbuffer.o modtimer.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
