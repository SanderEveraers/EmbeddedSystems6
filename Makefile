CROSS = /usr/local/xtools/arm-unknown-linux-uclibcgnueabi/bin/arm-linux-
KDIR := /home/student/felabs/sysdev/tinysystem/linux-2.6.34/

obj-m += pwm.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

cross:
	make ARCH=arm CROSS_COMPILE=$(CROSS) -C $(KDIR) M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
