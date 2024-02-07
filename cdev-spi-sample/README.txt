

Character device GPIOD/SPI module.
==================================

This sample driver sets up an interrupt handler on an input PIN (READY).
Whenever READY is changed an IRQ handler is called (in another thread).
The handler reads a number of bytes on SPI each time it is called.

Device is accessed from user space by a character device.

 
           Rasperry Pi 4                                    STM32F411
           Kernel module                                    HAL

     +--------------------------+                        +------------+
     |                          |                        |            |
     |  Kernel module           |                        | Device     |
     |                          |                        |            |
     |   +-----------------+    |                        |            |
     |   |                 |    |                        |            |
     |   |                 |    |     READY              |            |
     |   |  gpiod consumer +----+-----<------------------+            |
     |   |                 |    |                        |            |
     |   |                 |    |     BUSY               |            |
     |   |                 +----+----->------------------+            |
     |   |                 |    |                        |            |
     |   +-----------------+    |                        |            |
     |   +-----------------+    |                        |            |
     |   |                 |    |                        |            |
     |   | spi protocol    |    |     CLK                |            |
     |   | driver          +----+----->------------------+            |
     |   |                 |    |                        |            |
     |   |                 |    |     MOSI               |            |
     |   |                 +----+----->------------------+            |
     |   |                 |    |                        |            |
cdev |   |                 |    |     MISO               |            |
-----+---+                 +----+-----<------------------+            |
     |   |                 |    |                        |            |
     |   |                 |    |     CS0                |            |
     |   |                 +----+----->------------------+            |
     |   |                 |    |                        |            |
     |   +-----------------+    |                        |            |
     |                          |                        |            |
     +--------------------------+                        +------------+

Howto run on Raspberry Pi
	make
	sudo dtoverlay -d . cdev-spi-sample.dtbo
	sudo insmod cdev-spi-sample.ko
	dmesg -w
	.
	.
	.
	sudo rmmod cdev-spi-sample.ko

