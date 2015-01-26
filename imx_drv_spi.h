/**
 * \file	imx_drv_spi.h
 * \date	2014-Jun-11
 * \author	Andre Renaud
 * \copyright	Aiotec Ltd/Bluewater Systems
 * \brief
 * \details
 */
#ifndef IMX_DRV_SPI_H
#define IMX_DRV_SPI_H

#include "imx_usb_lib.h"

int imx_spi_init(struct libusb_device_handle *h, int spi_dev,
		int cs, int speed, unsigned int mode);
int imx_spi_xfer(struct libusb_device_handle *h, int spi_dev,
		unsigned int gpio_cs, uint8_t *tx, uint8_t *rx, int len);
int imx_spi_close(struct libusb_device_handle *h, int spi_dev);

#endif
