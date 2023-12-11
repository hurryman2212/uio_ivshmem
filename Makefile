# Add compiler flag(s)!
ccflags-y += -frecord-gcc-switches
ccflags-y += -save-temps -fverbose-asm
ccflags-y += -Wall -Werror -std=gnu11
ccflags-y += -march=native -O2 -ftree-vectorize -fvect-cost-model=very-cheap
ccflags-y += -I/usr/local/include/ -I/usr/include/
ccflags-y += -L/lib/modules/$(KVER)/updates/dkms/
ldflags-y +=

# Add path(s) of external C source or static library file(s) to temporarily copy to the local build directory!
EXTERN-HEADER :=
EXTERN-OBJECT :=
EXTERN-SOURCE :=

# DO NOT EDIT THE BELOW!

M ?= $(shell pwd)
KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(shell uname -r)/build

TARGET := $(shell grep '^PACKAGE_NAME=' $(M)/dkms.conf | cut -d'=' -f2 | tr -d '"')
TARGET_VERSION := $(shell grep '^PACKAGE_VERSION=' $(M)/dkms.conf | cut -d'=' -f2 | tr -d '"')
ccflags-y += -D__TARGET_VERSION__=\"$(TARGET_VERSION)\"

EXTERN := $(EXTERN-HEADER) $(EXTERN-OBJECT) $(EXTERN-SOURCE)

# These will be passed to the Makefile of the kernel source.
MY-SRCS += $(filter-out $(wildcard $(M)/*.mod.c), $(wildcard $(M)/*.c))
MY-OBJS += $(MY-SRCS:.c=.o)
obj-m += $(TARGET).o
$(TARGET)-objs += $(notdir $(MY-OBJS)) $(notdir $(EXTERN-OBJECT))

all:
	cp -r $(EXTERN) $(M)/ 2>/dev/null || true
	make -C $(KDIR) KVER=$(shell uname -r) KDIR=/lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

	ar -rcs $(TARGET).a $(MY-OBJS)
	cp /var/lib/dkms/$(TARGET)/$(TARGET_VERSION)/build/$(TARGET).a /lib/modules/$(KVER)/updates/dkms/ 2>/dev/null || true

	rm -rf $(notdir $(EXTERN))

install:
	cp -r $(EXTERN) $(M)/ 2>/dev/null || true
	dkms install "$(M)" --force
	rm -rf $(notdir $(EXTERN))

	cp *.rules /etc/udev/rules.d/ 2>/dev/null || true
uninstall:
	dkms remove $(TARGET)/$(shell cat dkms.conf | grep PACKAGE_VERSION | sed 's/^[^\.]\+\=//' | sed -e 's/["]//g') --all --force || true
	rm -rf /usr/src/$(TARGET)-$(TARGET_VERSION)
	rm -f /lib/modules/$(KVER)/updates/dkms/$(TARGET).a

	rm -f $(shell printf " /etc/udev/rules.d/%s" $(notdir $(shell ls $(M)/*.rules 2>/dev/null))) 2>/dev/null || true

load: all
	insmod $(TARGET).ko
unload:
	rmmod $(TARGET)

clean:
	make -C $(KDIR) KVER=$(shell uname -r) KDIR=/lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm -rf $(notdir $(EXTERN))
distclean:
	git clean -xdf
