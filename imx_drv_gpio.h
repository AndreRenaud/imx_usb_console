/**
 * \file	imx_drv_gpio.h
 * \date	2014-Jun-11
 * \author	Andre Renaud
 * \copyright	Aiotec Ltd/Bluewater Systems
 * \brief	Minimal GPIO driver for i.MX6 via USB Serial Downloader
 */
#ifndef IMX_DRV_GPIO_H
#define IMX_DRV_GPIO_H

#include "imx_usb_lib.h"

#define MXC_GPIO(bank,pin) ((((bank) - 1) << 5) | (pin))

int gpio_set_direction(libusb_device_handle *h, uint32_t gpio, int output);
int gpio_get_direction(libusb_device_handle *h, uint32_t gpio);
int gpio_get_value(libusb_device_handle *h, uint32_t gpio);
int gpio_set_value(libusb_device_handle *h, uint32_t gpio, int value);

#endif
