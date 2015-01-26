/**
 * \file	imx_usb_console/imx_usb_lib.h
 * \date	2013-Sep-12
 * \author	Andre Renaud
 * \copyright	Aiotec Ltd/Bluewater Systems
 * \brief       Interface to the i.MX?? usb interface
 */
#ifndef IMX_USB_LIB_H
#define IMX_USB_LIB_H

#include <stdint.h>
#include <stdio.h>

#include <libusb-1.0/libusb.h>

/**
 * Connect to an i.MX?? device running the USB bootloader
 * If multiple devices are present, the first one seen will be used
 */
libusb_device_handle *imx_connect(void);
/**
 * Disconnect from an i.MX?? device
 */
void imx_disconnect(struct libusb_device_handle *h);

/**
 * Perform a bulk read
 * @param h i.MX?? USB connection handle
 * @param addr Address to read from
 * @param result Buffer to store read data in
 * @param count Number of records to read
 * @param format Width of data to read (ie: 32, 16 or 8)
 * @return < 0 on failure, >= 0 on success
 */
int imx_read_bulk(libusb_device_handle *h, uint32_t addr, uint8_t *result,
        int count, int format);

/**
 * Write a single 32-bit register
 * @param h i.MX?? USB connection handle
 * @param addr Address of register to write
 * @param data Data to write
 * @return < 0 on failure, >= 0 on success
 */
int imx_write_reg32(libusb_device_handle *h, uint32_t addr, uint32_t data);
int imx_read_reg32(libusb_device_handle *h, uint32_t addr, uint32_t *value);
int imx_write_reg16(libusb_device_handle *h, uint32_t addr, uint16_t data);
int imx_read_reg16(libusb_device_handle *h, uint32_t addr, uint16_t *value);
int imx_write_reg8(libusb_device_handle *h, uint32_t addr, uint8_t data);
int imx_read_reg8(libusb_device_handle *h, uint32_t addr, uint8_t *value);

/**
 * Perform a bulk write of data to a given address
 * @param h i.MX?? USB connection handle
 * @param addr Memory address to begin the write at
 * @param data Data to write
 * @param length number of bytes in 'data'
 * @return < 0 on failure, >= 0 on success
 */
int imx_write_bulk(libusb_device_handle *h, uint32_t addr, uint8_t *data,
        int length);

/**
 * Perform a DCD write - ie: a bulk write of different values to different
 * addresses
 * @param h i.MX?? USB connection handle
 * @param data Array of count-32-bit triples. Each triple consists of
 *              - a data width (32, 16 or 8)
 *              - an address
 *              - a value
 * @param count Number of triples to write (must be < 85)
 * @return < 0 on failure, >= 0 on success
 */
int imx_dcd_write(libusb_device_handle *h, uint32_t *data, int count);

/**
 * Begin executing code at a given address
 * Note: This has to write an IVT record just prior to the jump address,
 * so ensure that the area just proceeding the address is writable and doesn't
 * contain any useful data
 * Note: After this has been executed successfully, no further USB operations
 * can be run, as the i.MX?? will no longer be running the USB bootloader
 * @param h i.MX?? USB connection handle
 * @param addr Address to begin executing code from
 * @return < 0 on failure, >= 0 on success
 */
int imx_jump_address(libusb_device_handle *h, uint32_t addr);

#endif
