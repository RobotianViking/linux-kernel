

Sample GPIOD/SPI module.
========================

This sample driver sets up an interrupt handler on an input PIN (READY).
Whenever READY is changed an IRQ handler is called (in another thread).
The handler reads a number of bytes on SPI each time it is called.
 
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
  |   |                 |    |     MISO               |            |
  |   |                 +----+-----<------------------+            |
  |   |                 |    |                        |            |
  |   |                 |    |     CS0                |            |
  |   |                 +----+----->------------------+            |
  |   |                 |    |                        |            |
  |   +-----------------+    |                        |            |
  |                          |                        |            |
  +--------------------------+                        +------------+

Howto run on Raspberry Pi
	make
	sudo dtoverlay -d . spi-protocol-sample.dtbo
	sudo insmod spi-protocol-sample.ko
	dmesg -w
	.
	.
	.
	sudo rmmod spi-protocol-sample.ko

