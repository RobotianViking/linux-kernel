MODULE_NAME=cdev-spi-sample

obj-m := $(MODULE_NAME).o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

all default: modules dt
install: modules_install

modules modules_install help:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) $@

dt: $(MODULE_NAME).dtbo

$(MODULE_NAME).dtbo: $(MODULE_NAME).dts
	dtc -@ -I dts -O dtb -o $(MODULE_NAME).dtbo $(MODULE_NAME).dts

clean:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) $@
	rm -f $(MODULE_NAME).dtbo
