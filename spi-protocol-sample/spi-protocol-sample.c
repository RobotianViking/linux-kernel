#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/gpio/consumer.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/crc32.h>
#include <linux/timekeeping.h>

#define SPI_MODULE      "spi_protocol_sample"
#define RX_BUFFER_SIZE  10*1024*4
/*
 *    DTS fragments: see spi-protocol-sample.dts
 */

static int get_block_sync(struct spi_device *spidev, size_t n, u8 *buf);

/**
 * @brief Structure for holding device state across driver callbacks
 *
 */
typedef struct _drvdata_t
{
   int irq;
   struct gpio_desc *ready, *busy;
   struct spi_message *message;
   struct spi_device *spidevice;
   u8 *rx_data;
} drvdata_t;

static void dump_spi_device(struct spi_device *spidev) {
   dev_info(&spidev->dev, "SPI device dump\n");
   dev_info(&spidev->dev, "===============\n");
   dev_info(&spidev->dev, "max_speed_hz: %d\n", spidev->max_speed_hz);
   dev_info(&spidev->dev, "chip_select: %d\n", spidev->chip_select);
   dev_info(&spidev->dev, "bits_per_word: %d\n", spidev->bits_per_word);
   dev_info(&spidev->dev, "rt: %d\n", spidev->rt);
   dev_info(&spidev->dev, "mode: 0x%x\n", spidev->mode);
   dev_info(&spidev->dev, "irq: %d\n", spidev->irq);
   dev_info(&spidev->dev, "===============\n");
}

/*
 * IRQ handlers for READY pin
 */

/**
 * @brief Use default top half handler
 */
static irq_handler_t top_ready_handler = NULL;

/**
 * @brief Handle interrupt in bottom half (in another thread)
 */
static irqreturn_t bottom_ready_handler(int irq, void *dev_id) {
   int ready_pin;
   int retval;
   u32 recv_crc;
   u32 comp_crc;
   ktime_t spi_ticks;
   struct spi_device *spidev = (struct spi_device *) dev_id;
   drvdata_t *drvdata = spi_get_drvdata(spidev);

   /* Get state of READY GPIO pin */
   ready_pin = gpiod_get_value(drvdata->ready);

   dev_info(&spidev->dev, "start spi");

   gpiod_set_value(drvdata->busy, 1);
   spi_ticks = ktime_get();
   retval = get_block_sync(spidev, RX_BUFFER_SIZE, drvdata->rx_data);
   spi_ticks = ktime_get() - spi_ticks;
   gpiod_set_value(drvdata->busy, 0);
   dev_info(&spidev->dev, "ended spi (ktime delta = %lld nsecs)", spi_ticks);
   // dev_info(&spidev->dev, "spi rate = %f bytes/sec", (double) RX_BUFFER_SIZE*1000000000.0/(double) spi_ticks);
   if (retval) {
      dev_err(&spidev->dev, "get_block_sync failed %d\n", retval);
      return IRQ_HANDLED;
   }

#if 0
   x = *((u32*) (drvdata->rx_data));
   dev_info(&spidev->dev, "x = 0x%x\n", x);
   comp_crc = ether_crc(4, (unsigned char *) drvdata->rx_data);
   dev_info(&spidev->dev, "ether_crc:    CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = ether_crc_le(4, (unsigned char *) drvdata->rx_data);
   dev_info(&spidev->dev, "ether_crc_le: CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_le(~0, (unsigned char *) drvdata->rx_data, 4);
   dev_info(&spidev->dev, "crc_le:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_be(~0, (unsigned char *) drvdata->rx_data, 4);
   dev_info(&spidev->dev, "crc_be:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_le(0, (unsigned char *) drvdata->rx_data, 4);
   dev_info(&spidev->dev, "crc_le:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_be(0, (unsigned char *) drvdata->rx_data, 4);
   dev_info(&spidev->dev, "crc_be:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   x = __builtin_bswap32(x);
   dev_info(&spidev->dev, "x = 0x%x\n", x);
   comp_crc = ether_crc(4, (unsigned char *) &x);
   dev_info(&spidev->dev, "ether_crc:    CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = ether_crc_le(4, (unsigned char *) &x);
   dev_info(&spidev->dev, "ether_crc_le: CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_le(~0, (unsigned char *) &x, 4);
   dev_info(&spidev->dev, "crc_le:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_be(~0, (unsigned char *) &x, 4);
   dev_info(&spidev->dev, "crc_be:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   //comp_crc = crc32_le(0xffffffff, drvdata->rx_data, 4); //RX_BUFFER_SIZE - 4);
   //comp_crc = ether_crc_le(drvdata->rx_data, RX_BUFFER_SIZE - 4);
   dev_info(&spidev->dev, "[%s]\n", drvdata->rx_data);
#endif
   comp_crc = crc32_be(~0, (unsigned char *) drvdata->rx_data, RX_BUFFER_SIZE - sizeof(u32));
   recv_crc = *((u32 *) (&drvdata->rx_data[RX_BUFFER_SIZE - sizeof(u32)]));
   if (comp_crc != recv_crc) {
      dev_err(&spidev->dev, "Transmission error on SPI. Got CRC 0x%x, expected 0x%x\n", recv_crc, comp_crc);
      return IRQ_HANDLED;
   }
#if 0   
   dev_info(&spidev->dev, "crc_be:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   dev_info(&spidev->dev, "Computed CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   dev_info(&spidev->dev, "Received CRC 0x%x\n", recv_crc);
#endif
   dev_info(&spidev->dev, "READY state is %d, irq=%d\n", ready_pin, irq);
   
   /* Copy READY state to BUSY pin - just for fun */
   // gpiod_set_value(drvdata->busy, ready_pin);

   return IRQ_HANDLED;
}

/**
 * @brief Get block of data in synchronous mode
*/
static int get_block_sync(struct spi_device *spidev, size_t n, u8 *buf) {
   int retval;
   struct spi_message msg;
   struct spi_transfer t = {
      .speed_hz = spidev->max_speed_hz,
      .rx_buf = buf,
      .len = n
   };

   spi_message_init(&msg);
   spi_message_add_tail(&t, &msg);

   retval = spi_sync(spidev, &msg);

   return retval;
}

static int spi_probe(struct spi_device *spidev) {
   int retval;
   drvdata_t *drvdata;

   dev_info(&spidev->dev, "spi_probe\n");

   drvdata = (drvdata_t *)devm_kzalloc(&spidev->dev, sizeof(drvdata_t), GFP_KERNEL);
   drvdata->rx_data = (u8 *) devm_kzalloc(&spidev->dev, RX_BUFFER_SIZE+4, GFP_KERNEL);

   retval = spi_setup(spidev);
   if (retval < 0) {
      dev_err(&spidev->dev, "Unable to setup SPI. Returned %d\n", retval);
      return retval;
   }
   drvdata->spidevice = spidev;
   dev_info(&spidev->dev, "spi_probe success\n");
   drvdata->busy = gpiod_get_index(&spidev->dev, "busy", 0, GPIOD_OUT_LOW);
   if (IS_ERR(drvdata->busy)) {
      dev_err(&spidev->dev, "gpiod_get_index failed for BUSY\n");
      return PTR_ERR(drvdata->busy);
   }

   drvdata->ready = gpiod_get(&spidev->dev, "ready", GPIOD_IN);
   if (IS_ERR(drvdata->ready)) {
      dev_err(&spidev->dev, "gpiod_get failed for READY\n");
      return PTR_ERR(drvdata->busy);
   }

   drvdata->irq = gpiod_to_irq(drvdata->ready);
   if (drvdata->irq < 0) {
      dev_err(&spidev->dev, "gpiod_to_irq failed for READY pin\n");
      return drvdata->irq;
   }
   dev_info(&spidev->dev, "GPIOD irq = %d.\n", drvdata->irq);

   retval = request_threaded_irq(drvdata->irq,
                                 top_ready_handler,
                                 bottom_ready_handler,
                                 IRQF_TRIGGER_RISING  | IRQF_ONESHOT,
                                 "spi-protocol-sample",
                                 spidev);

   spi_set_drvdata(spidev, drvdata);

   dump_spi_device(spidev);

   dev_info(&spidev->dev, "GPIOD part probed succesfully.\n");

   return 0;
};

static int spi_remove(struct spi_device *spidev)
{
   drvdata_t *drvdata = spi_get_drvdata(spidev);
   if (!drvdata)
   {
      dev_err(&spidev->dev, "Could not get driver data.\n");
      return -ENODEV;
   }
   free_irq(drvdata->irq, spidev);
   gpiod_put(drvdata->ready);
   gpiod_put(drvdata->busy);
   dev_info(&spidev->dev, "spi_remove\n");
   return 0;
};

static const struct of_device_id spi_dt_ids[] = {
    {
        .compatible = "nisse,spi-protocol-device",
    },
    {}};

static struct spi_driver spi_driver = {
    .probe = spi_probe,
    .remove = spi_remove,
    .driver = {
        .name = "spi_protocol_driver",
        .of_match_table = of_match_ptr(spi_dt_ids),
        .owner = THIS_MODULE,
    },
};

static int __init spi_module_init(void)
{
   pr_info("%s: module init\n", SPI_MODULE);
   /* Register spi driver */
   spi_register_driver(&spi_driver);
   return 0;
}

static void __exit spi_module_exit(void)
{
   spi_unregister_driver(&spi_driver);
   pr_info("%s: module exit\n", SPI_MODULE);
}

module_init(spi_module_init);
module_exit(spi_module_exit);

MODULE_AUTHOR("Niels Prösch, <niels.h.prosch@gmail.com>");
MODULE_LICENSE("GPL");
