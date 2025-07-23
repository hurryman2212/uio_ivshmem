# Add compiler flag(s)!
ccflags-y += -frecord-gcc-switches -fdiagnostics-color=always
ccflags-y += -Wall -Werror
ccflags-y += -Wall -Werror -std=gnu11 -D_GNU_SOURCE
ccflags-y += -O2 -DNDEBUG -march=native -ftree-vectorize
ccflags-y += -I/usr/include -I/usr/local/include
ccflags-y +=
ldflags-y +=

# Add path(s) of external C source or static library file(s) to temporarily copy to the local build directory!
EXTERN-HEADER :=
EXTERN-OBJECT :=
EXTERN-SOURCE :=

# DO NOT EDIT THE BELOW!

SHELL := /bin/bash

M ?= $(shell pwd)
KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(shell uname -r)/build

TARGET := $(shell grep '^PACKAGE_NAME=' $(M)/dkms.conf | cut -d'=' -f2 | tr -d '"')
PACKAGE_VERSION := $(shell grep '^PACKAGE_VERSION=' $(M)/dkms.conf | cut -d'=' -f2 | tr -d '"')
ccflags-y += -D__PACKAGE_VERSION__=\"$(PACKAGE_VERSION)\"

EXTERN := $(EXTERN-HEADER) $(EXTERN-OBJECT) $(EXTERN-SOURCE)

# These will be passed to the Makefile of the kernel source.
MY-SRCS += $(filter-out $(wildcard $(M)/*.mod.c), $(wildcard $(M)/*.c))
MY-OBJS += $(MY-SRCS:.c=.o)
obj-m += $(TARGET).o
$(TARGET)-objs += $(notdir $(MY-OBJS)) $(notdir $(EXTERN-OBJECT))

LLVM_OPT := $(shell grep -q '^#define CONFIG_CC_IS_GCC 1' $(KDIR)/include/generated/autoconf.h 2>/dev/null || echo LLVM=1)

all:
	-[[ "$(M)" != *"/dkms/"* ]] && rm -rf $(notdir $(EXTERN))

	cp -r $(EXTERN) $(M)/ 2>/dev/null || :
	make -C $(KDIR) KVER=$(shell uname -r) KDIR=/lib/modules/$(shell uname -r)/build M=$(shell pwd) $(LLVM_OPT) modules

	ar -rcs $(TARGET).a $(MY-OBJS)
	cp /var/lib/dkms/$(TARGET)/$(PACKAGE_VERSION)/build/$(TARGET).a /lib/modules/$(KVER)/updates/dkms/ 2>/dev/null || :

	rm -rf $(notdir $(EXTERN))

install:
	rm -rf $(notdir $(EXTERN))

	cp -r $(EXTERN) $(M)/ 2>/dev/null || :
	dkms install "$(M)" --force && cp *.rules /etc/udev/rules.d/ 2>/dev/null || :

	rm -rf $(notdir $(EXTERN))
uninstall:
	dkms remove $(TARGET)/$(shell cat dkms.conf | grep PACKAGE_VERSION | sed 's/^[^\.]\+\=//' | sed -e 's/["]//g') --all --force || :
	rm -rf /usr/src/$(TARGET)-$(PACKAGE_VERSION)
	rm -f /lib/modules/$(KVER)/updates/dkms/$(TARGET).a

	rm -f $(shell printf " /etc/udev/rules.d/%s" $(notdir $(shell ls $(M)/*.rules 2>/dev/null))) 2>/dev/null || :

load: all
	insmod $(TARGET).ko
unload:
	rmmod $(TARGET)

clean:
	make -C $(KDIR) KVER=$(shell uname -r) KDIR=/lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm -rf $(notdir $(EXTERN))
distclean:
	git clean -xdf
