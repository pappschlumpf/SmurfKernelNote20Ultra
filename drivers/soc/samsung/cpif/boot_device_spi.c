/*
 * Copyright (C) 2014-2019, Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>

#include "modem_prj.h"
#include "modem_utils.h"
#include "link_device_memory.h"
#include "boot_device_spi.h"

#define MAX_IMAGE_SIZE	SZ_128K
#define MAX_SPI_DEVICE	2

static int _count;
static struct cpboot_spi _cpboot[MAX_SPI_DEVICE];

/*
 * Export functions
 */
int cpboot_spi_load_cp_image(struct link_device *ld, struct io_device *iod, unsigned long arg)
{
	int ret = 0;
	struct mem_link_device *mld = ld_to_mem_link_device(ld);
	struct cpboot_spi *cpboot = mld->boot_spi;
	struct cp_image img;
	char *buff = NULL;
	struct spi_message msg;
	struct spi_transfer xfer;

	if (!cpboot->spi) {
		mif_err("spi is null\n");
		return -EPERM;
	}

	mutex_lock(&cpboot->lock);

	ret = copy_from_user(&img, (const void __user *)arg, sizeof(struct cp_image));
	if (ret) {
		mif_err("copy_from_user() arg error:%d\n", ret);
		goto exit;
	}

	mif_info("size:%d bus_num:%d\n", img.size, cpboot->spi->controller->bus_num);
	if ((img.size == 0) || (img.size > MAX_IMAGE_SIZE))  {
		mif_err("size error:%d\n", img.size);
		ret = -EINVAL;
		goto exit;
	}

	buff = vzalloc(img.size);
	if (!buff) {
		mif_err("vzalloc(%u) error\n", img.size);
		ret = -ENOMEM;
		goto exit;
	}

	ret = copy_from_user(buff, (const void __user *)img.binary, img.size);
	if (ret) {
		mif_err("copy_from_user() buff error:%d\n", ret);
		goto exit;
	}

	memset(&xfer, 0, sizeof(struct spi_transfer));
	xfer.len = img.size;
	xfer.tx_buf = buff;
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(cpboot->spi, &msg);
	if (ret < 0) {
		mif_err("spi_sync() error:%d\n", ret);
		goto exit;
	}

exit:
	if (buff)
		vfree(buff);
	mutex_unlock(&cpboot->lock);

	return ret;
}
EXPORT_SYMBOL(cpboot_spi_load_cp_image);

struct cpboot_spi *cpboot_spi_get_device(int bus_num)
{
	int i;
	struct cpboot_spi *cpboot = NULL;

	for (i = 0; i < MAX_SPI_DEVICE; i++) {
		if (_cpboot[i].spi && (bus_num == _cpboot[i].spi->controller->bus_num)) {
			mif_info("Get bus_num:%d\n", bus_num);
			cpboot = &_cpboot[i];
			return cpboot;
		}
	}

	mif_err("Can not get bus_num:%d\n", bus_num);

	return NULL;
}
EXPORT_SYMBOL(cpboot_spi_get_device);

/*
 * Probe
 */
static int cpboot_spi_probe(struct spi_device *spi)
{
	int ret = 0;

	mif_info("bus_num:%d count:%d\n", spi->controller->bus_num, _count);

	if (_count >= MAX_SPI_DEVICE) {
		mif_err("_count is over %d\n", MAX_SPI_DEVICE);
		return -EINVAL;
	}

	mutex_init(&_cpboot[_count].lock);

	spi->bits_per_word = 8;
	if (spi_setup(spi)) {
		mif_err("ERR! spi_setup fail\n");
		ret = -EINVAL;
		goto err_setup;
	}
	spi_set_drvdata(spi, &_cpboot[_count]);
	_cpboot[_count].spi = spi;

	_count++;
	return 0;

err_setup:
	mutex_destroy(&_cpboot[_count].lock);

	return ret;
}

static int cpboot_spi_remove(struct spi_device *spi)
{
	struct cpboot_spi *cpboot = spi_get_drvdata(spi);

	mutex_destroy(&cpboot->lock);

	return 0;
}

static const struct of_device_id cpboot_spi_dt_match[] = {
	{ .compatible = "samsung,exynos-cp-spi" },
	{ }
};
MODULE_DEVICE_TABLE(of, cpboot_spi_dt_match);

static struct spi_driver cpboot_spi_driver = {
	.driver = {
		.name = "cpboot_spi",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cpboot_spi_dt_match),
	},
	.probe = cpboot_spi_probe,
	.remove = cpboot_spi_remove,
};

static int __init cpboot_spi_init(void)
{
	int ret = 0;

	ret = spi_register_driver(&cpboot_spi_driver);
	if (ret) {
		mif_err("spi_register_driver() error:%d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit cpboot_spi_exit(void)
{
	spi_unregister_driver(&cpboot_spi_driver);
}

module_init(cpboot_spi_init);
module_exit(cpboot_spi_exit);

MODULE_DESCRIPTION("Exynos SPI driver to load CP bootloader");
MODULE_LICENSE("GPL");
