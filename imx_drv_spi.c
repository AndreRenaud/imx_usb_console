/**
 * \file	imx_drv_spi.c
 * \date	2014-Jun-11
 * \author	Andre Renaud
 * \copyright	Aiotec Ltd/Bluewater Systems
 * \brief
 * \details
 */
#include "imx_drv_spi.h"
#include "imx_drv_gpio.h"

enum {
	ECSPI_RXDATA	= 0x00,
	ECSPI_TXDATA	= 0x04,
	ECSPI_CONREG	= 0x08,
	ECSPI_CONFIGREG	= 0x0c,
	ECSPI_INTREG	= 0x10,
	ECSPI_DMAREG	= 0x14,
	ECSPI_STATREG	= 0x18,
	ECSPI_PERIODREG	= 0x1c,
	ECSPI_TESTREG	= 0x20,
	ECSPI_MSGDATA	= 0x40,
};

/* For the i.MX6, from ECSPI memory map, 21.7, IMX6DQRM.pdf */
static uint32_t ecspi_base_addr[] = {
	0x02008000,
	0x0200c000,
	0x02010000,
	0x02014000,
	0x02018000,
};

static int ecspi_write(struct libusb_device_handle *h, int spi_dev,
		int reg, uint32_t val)
{
	return imx_write_reg32(h, ecspi_base_addr[spi_dev] + reg, val);
}

static int ecspi_read(struct libusb_device_handle *h, int spi_dev,
		int reg, uint32_t *val)
{
	//return imx_read_reg32(h, ecspi_base_addr[spi_dev] + reg, val);
	return imx_read_bulk(h, ecspi_base_addr[spi_dev] + reg,
				(uint8_t *)val, 1, 0x20);
}

static int ecspi_setbits(struct libusb_device_handle *h, int spi_dev,
		int reg, uint32_t bits)
{
	int e;
	uint32_t val;

	e = imx_read_reg32(h, ecspi_base_addr[spi_dev] + reg, &val);
	if (e < 0)
		return e;
	//printf("Changing 0x%x from 0x%x to 0x%x\n",
			//ecspi_base_addr[spi_dev] + reg, val, val | bits);
	val |= bits;
	return imx_write_reg32(h, ecspi_base_addr[spi_dev] + reg, val);

}

int imx_spi_init(struct libusb_device_handle *h, int spi_dev,
		int cs, int speed, unsigned int mode)
{
	uint32_t con_reg =
		0 << 20 |
		cs << 18 |
		0 << 16 |
		8 << 12 | // FIXME: PRE DIVIDER
		8 << 8 | // FIXME: POST DIVIDER
		0xf << 4 |
		0 << 3 |
		0 << 2 |
		0 << 1 |
		1 << 0;
	uint32_t config_reg =
		0x0 << 20 | // FIXME: SCLK_CTL
		0x0 << 16 | // FIXME: DATA_CTL
		0x0 << 12 | // FIXME: SS_POL
		0x0 << 8 | // FIXME: SS_CTL
		0x0 << 4 | // FIXME: SCLK_POL
		0x0 << 0; // FIXME: SCLK_PHA

	printf("Configuring spi 0x%x 0x%x %d 0x%x\n",
			spi_dev, cs, speed, mode);

	if (ecspi_write(h, spi_dev, ECSPI_CONREG, con_reg) < 0)
		return -1;
	if (ecspi_write(h, spi_dev, ECSPI_CONFIGREG, config_reg) < 0)
		return -1;

	/* Clear any outstanding issues */
	if (ecspi_write(h, spi_dev, ECSPI_INTREG, 0) < 0)
		return -1;
	if (ecspi_write(h, spi_dev, ECSPI_STATREG, 1 << 7 | 1 << 6) < 0)
		return -1;

	return 0;
}

static int imx_spi_xfer_block(struct libusb_device_handle *h, int spi_dev,
	uint8_t *tx, uint8_t *rx, int len)
{
	int i;
	uint32_t v;

	/* Set up the transfer length */
	if (ecspi_read(h, spi_dev, ECSPI_CONREG, &v) < 0)
		return -1;

	v &= 0x000fffff;
	v |= ((len * 8) - 1) << 20;
	printf("Bits: 0x%x (0x%x)\n", ((len * 8) - 1), v);
	if (ecspi_write(h, spi_dev, ECSPI_CONREG, v) < 0)
		return -1;

	printf("Writing %d bytes of data\n", len);

	/* Clear any outstanding issues */
	if (ecspi_write(h, spi_dev, ECSPI_STATREG, 1 << 7 | 1 << 6) < 0)
		return -1;

	/* Start by aligning the first sub-word bytes */
	if (len % 4) {
		uint32_t data = 0;
		if (tx)
			for (i = 0; i < len % 4; i++)
				data = (data << 8) | tx[i];
		printf("Writing 0x%x\n", data);
		if (ecspi_write(h, spi_dev, ECSPI_TXDATA, data) < 0)
			return -1;
	}


	/* Write the data as 32-bit words */
	for (i = len % 4; i < len; i+= 4) {
		uint32_t data = 0;
		if (tx) {
			data = tx[i] << 24;
			if (i + 1 < len)
				data |= tx[i + 1] << 16;
			if (i + 2 < len)
				data |= tx[i + 2] << 8;
			if (i + 3 < len)
				data |= tx[i + 3];
		}
		printf("Writing 0x%x\n", data);
		if (ecspi_write(h, spi_dev, ECSPI_TXDATA, data) < 0)
			return -1;
	}

	ecspi_read(h, spi_dev, ECSPI_TESTREG, &v);
	printf("Test register: 0x%x\n", v);

	/* Enable the ECSPI controller & start the transfer */
	if (ecspi_setbits(h, spi_dev, ECSPI_CONREG, 1 << 2 | 1 << 0) < 0)
		return -1;

	ecspi_read(h, spi_dev, ECSPI_CONREG, &v);
	printf("Control register: 0x%x\n", v);
	//ecspi_read(h, spi_dev, ECSPI_CONFIGREG, &v);
	//printf("Config register: 0x%x\n", v);
	//ecspi_read(h, spi_dev, ECSPI_STATREG, &v);
	//printf("Waiting for completion: 0x%x\n", v);
	/* Wait for the transfer to complete */
	while (1) {
		uint32_t v;
		if (ecspi_read(h, spi_dev, ECSPI_STATREG, &v) < 0)
			return -1;
		//printf("Stat reg: 0x%x\n", v);
		if (v & (1 << 7))
			break;
	}

	/* Clear any outstanding issues */
	//if (ecspi_write(h, spi_dev, ECSPI_STATREG, 1 << 7 | 1 << 6) < 0)
		//return -1;
	ecspi_read(h, spi_dev, ECSPI_TESTREG, &v);
	printf("Test register: 0x%x\n", v);

	printf("Reading %d bytes of response\n", len);
	/* Read the response data */
	for (i = 0; i < len; i += 4) {
		uint32_t data;
		ecspi_read(h, spi_dev, ECSPI_TESTREG, &v);
		printf("Test register before %d: 0x%x\n", i, v);
		if (imx_read_reg32(h, ecspi_base_addr[spi_dev] + ECSPI_RXDATA, &data) < 0)
			return -1;
		//if (ecspi_read(h, spi_dev, ECSPI_RXDATA, &data) < 0)
			//return -1;
		printf("Got %d 0x%x\n", i, data);
		ecspi_read(h, spi_dev, ECSPI_TESTREG, &v);
		printf("Test register after %d: 0x%x\n", i, v);
		if (rx) {
			rx[i] = data >> 24;
			if (i + 1 < len)
				rx[i + 1] = data >> 16;
			if (i + 2 < len)
				rx[i + 2] = data >> 8;
			if (i + 3 < len)
				rx[i + 3] = data;
		}
	}

	return 0;
}

int imx_spi_xfer(struct libusb_device_handle *h, int spi_dev,
		unsigned int gpio_cs, uint8_t *tx, uint8_t *rx, int len)
{
	/* FIXME: Break it down into separate transfers */
	if (len > 64) {
		fprintf(stderr, "ECSPI only has a 64-byte fifo\n");
		return -1;
	}

	/* Lower the chip select */
	if (gpio_set_direction(h, gpio_cs, 1) < 0)
		return -1;
	if (gpio_set_value(h, gpio_cs, 0) < 0)
		return -1;

	/* FIXME: Break it down into separate FIFO sized blocks */
	if (imx_spi_xfer_block(h, spi_dev, tx, rx, len) < 0)
		return -1;

	/* Raise the chip select */
	if (gpio_set_value(h, gpio_cs, 1) < 0)
		return -1;

	return 0;
}

int imx_spi_close(struct libusb_device_handle *h, int spi_dev)
{
	/* Disable the whole thing */
	if (ecspi_write(h, spi_dev, ECSPI_CONREG, 0) < 0)
		return -1;
	return 0;
}
