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

/**
 * @brief Structure for holding device state across driver callbacks
 */
typedef struct _drvdata_t
{
   int irq;                         /* IRQ for ready pin */
   struct gpio_desc *ready,         /* Input pin. Raised when data is ready on MCU */
                    *busy;          /* Out pin. Raised by when busy (tramitting) */
   u8 *rx_data;                     /* Data buffer for receiving */
} drvdata_t;

/**
 * Forward declarations
*/
static int get_block_sync(struct spi_device *spidev, size_t n, u8 *buf);
static irq_handler_t top_ready_handler = NULL; /* Use default top half */
static irqreturn_t bottom_ready_handler(int irq, void *dev_id);
static int get_block_sync(struct spi_device *spidev, size_t n, u8 *buf);
static int spi_probe(struct spi_device *spidev);
static int spi_remove(struct spi_device *spidev);
static int __init spi_module_init(void);
static void __exit spi_module_exit(void);

/**
 * @brief Dump spi device info for debug
*/
// #define DEBUG_DUMP_SPI
#ifdef DEBUG_DUMP_SPI
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
#define DEBUG_DUMP_SPI_DEVICE(dev) dump_spi_device(dev)
#else
#define DEBUG_DUMP_SPI_DEVICE(dev) 
#endif

// #define DEBUG_DUMP_CRC
#ifdef DEBUG_DUMP_CRC
/**
 * @brief Dump misc CRC32 computations for debug
*/
static void dump_crc32(struct device *dev) {
   drvdata_t *drvdata = (drvdata_t *) dev_get_drvdata(dev);
   u32 comp_crc = 0;
   u32 recv_crc = *( (u32*) &drvdata->rx_data[RX_BUFFER_SIZE - sizeof(u32)]);
   dev_info(dev, "Received CRC: 0x%x\n", recv_crc);
   comp_crc = ether_crc(RX_BUFFER_SIZE - sizeof(u32), (unsigned char *) drvdata->rx_data);
   dev_info(dev, "ether_crc:    CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = ether_crc_le(RX_BUFFER_SIZE - sizeof(u32), (unsigned char *) drvdata->rx_data);
   dev_info(dev, "ether_crc_le: CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_le(~0, (unsigned char *) drvdata->rx_data, RX_BUFFER_SIZE - sizeof(u32));
   dev_info(dev, "crc_le:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_be(~0, (unsigned char *) drvdata->rx_data, RX_BUFFER_SIZE - sizeof(u32));
   dev_info(dev, "crc_be:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_le(0, (unsigned char *) drvdata->rx_data, RX_BUFFER_SIZE - sizeof(u32));
   dev_info(dev, "crc_le:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);
   comp_crc = crc32_be(0, (unsigned char *) drvdata->rx_data, RX_BUFFER_SIZE - sizeof(u32));
   dev_info(dev, "crc_be:       CRC 0x%x, ICRC 0x%x\n", comp_crc, comp_crc ^ 0xffffffff);

   dev_info(dev, "[%s]\n", drvdata->rx_data);
}
#define DEBUG_DUMP_CRC32(dev) dump_crc32(dev)
#else
#define DEBUG_DUMP_CRC32(dev) 
#endif

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
   if (retval) {
      dev_err(&spidev->dev, "get_block_sync failed %d\n", retval);
      return IRQ_HANDLED;
   }
   dev_info(&spidev->dev, "ended spi (ktime delta = %lld nsecs)", spi_ticks);

   DEBUG_DUMP_CRC32(&spidev->dev);
   comp_crc = crc32_be(~0, (unsigned char *) drvdata->rx_data, RX_BUFFER_SIZE - sizeof(u32));
   recv_crc = *((u32 *) (&drvdata->rx_data[RX_BUFFER_SIZE - sizeof(u32)]));
   if (comp_crc != recv_crc) {
      dev_err(&spidev->dev, "Transmission error on SPI. Got CRC 0x%x, expected 0x%x\n", recv_crc, comp_crc);
      return IRQ_HANDLED;
   }
   dev_info(&spidev->dev, "READY state is %d, irq=%d\n", ready_pin, irq);
   
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

/**
 * @brief Probe SPI and GPIO
*/
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
   dev_info(&spidev->dev, "spi_probe success\n");
   drvdata->busy = devm_gpiod_get(&spidev->dev, "busy", GPIOD_OUT_LOW);
   if (IS_ERR(drvdata->busy)) {
      dev_err(&spidev->dev, "gpiod_get_index failed for BUSY\n");
      return PTR_ERR(drvdata->busy);
   }

   drvdata->ready = devm_gpiod_get(&spidev->dev, "ready", GPIOD_IN);
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

   DEBUG_DUMP_SPI_DEVICE(spidev);

   dev_info(&spidev->dev, "GPIOD part probed succesfully.\n");

   return 0;
};

static int spi_remove(struct spi_device *spidev)
{
   drvdata_t *drvdata = spi_get_drvdata(spidev);
   if (!drvdata)
   {
      dev_err(&spidev->dev, "Could not get driver data (remove).\n");
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
    { /* sentry */ }
};

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
