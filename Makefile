obj-m += webcam_res_filter.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	sudo $(MAKE) -C $(KDIR) M=$(PWD) modules_install
	sudo depmod -a

load:
	sudo insmod webcam_res_filter.ko

unload:
	sudo rmmod webcam_res_filter

reload: unload load

test:
	v4l2-ctl --list-formats-ext

.PHONY: all clean install load unload reload test