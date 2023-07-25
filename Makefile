MODULE_NAME = uio_ivshmem

obj-m += $(MODULE_NAME).o

KDIR ?= /lib/modules/$(shell uname -r)

all:
	make -C $(KDIR)/build M=$(shell pwd) modules

install: all
	dkms install "$(shell pwd)" --force
	cp *.rules /etc/udev/rules.d/
uninstall: *.rules
	dkms remove $(MODULE_NAME)/$(shell cat dkms.conf | grep PACKAGE_VERSION | sed 's/^[^\.]\+\=//' | sed -e 's/["]//g') --all
	rm -f $(shell printf "/etc/udev/rules.d/%s " $^)

load: all
	modprobe uio
	insmod $(MODULE_NAME).ko
unload:
	rmmod $(MODULE_NAME)
	modprobe -r uio

clean:
	rm -f *.ko *.o *.mod* *.symvers *.order .*.cmd
