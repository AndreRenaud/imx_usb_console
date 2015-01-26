/**
 * \file	imx_drv_gpio.c
 * \date	2014-Jun-11
 * \author	Andre Renaud
 * \copyright	Aiotec Ltd/Bluewater Systems
 * \brief	Minimal GPIO driver for i.MX6 via USB Serial Downloader
 */
#include "imx_drv_gpio.h"

/* For the iMX6, from "GPIO memory map", 28.5 of IMX6SQRM.pdf */
static uint32_t gpio_base[] = {
	0x0209c000, 0x020a0000, 0x020a4000, 0x020a8000,
	0x020ac000, 0x020b0000, 0x020b4000
};
#define ARRAY_SIZE(a) ((sizeof(a)) / sizeof((a)[0]))

enum {
	GPIO_DR		= 0x00,
	GPIO_GDIR	= 0x04,
	GPIO_PSR	= 0x08,
	GPIO_ICR1	= 0x0c,
	GPIO_ICR2	= 0x10,
	GPIO_IMR	= 0x14,
	GPIO_ISR	= 0x18,
	GPIO_EDGE_SEL	= 0x1c,
};

static uint32_t gpio_to_pinmask(uint32_t gpio)
{
	return 1 << (gpio & 0x1f);
}

static uint32_t gpio_to_base(uint32_t gpio)
{
	int bank = gpio >> 5;
	if (bank < 0 || bank >= ARRAY_SIZE(gpio_base))
		bank = 0; // FIXME: NO FAILING?
	return gpio_base[bank];
}

int gpio_set_direction(libusb_device_handle *h, uint32_t gpio, int output)
{
	uint32_t val;
	int e;
	uint32_t base = gpio_to_base(gpio);
	uint32_t mask = gpio_to_pinmask(gpio);

	e = imx_read_reg32(h, base + GPIO_GDIR, &val);
	if (e < 0)
		return e;
	if (output)
		val |= mask;
	else
		val &= ~mask;
	e = imx_write_reg32(h, base + GPIO_GDIR, val);
	return e;
}

int gpio_get_direction(libusb_device_handle *h, uint32_t gpio)
{
	uint32_t val;
	int e;
	uint32_t base = gpio_to_base(gpio);
	uint32_t mask = gpio_to_pinmask(gpio);

	e = imx_read_reg32(h, base + GPIO_GDIR, &val);
	if (e < 0)
		return e;
	return (val & mask) ? 1 : 0;
}

int gpio_get_value(libusb_device_handle *h, uint32_t gpio)
{
	uint32_t val;
	int e;
	uint32_t base = gpio_to_base(gpio);
	uint32_t mask = gpio_to_pinmask(gpio);

	e = imx_read_reg32(h, base + GPIO_PSR, &val);
	if (e < 0)
		return e;
	return (val & mask) ? 1 : 0;
}

int gpio_set_value(libusb_device_handle *h, uint32_t gpio, int value)
{
	uint32_t val;
	int e;
	uint32_t base = gpio_to_base(gpio);
	uint32_t mask = gpio_to_pinmask(gpio);

	e = imx_read_reg32(h, base + GPIO_DR, &val);
	if (e < 0)
		return e;
	if (value)
		val |= mask;
	else
		val &= ~mask;
	e = imx_write_reg32(h, base + GPIO_DR, val);
	return e;
}

