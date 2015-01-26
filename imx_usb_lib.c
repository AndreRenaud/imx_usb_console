/**
 * \file	imx_usb_console/imx_usb_lib.c
 * \date	2013-Sep-12
 * \author	Andre Renaud
 * \copyright	Aiotec Ltd/Bluewater Systems
 * \brief       Implementation of the USB Downloader Protocol for the i.MX??
 * \description
 * See I.MX50 Application Processor Reference Manual, Rev 1, 10/2011
 *          IMX50RM.pdf, Chapter 6.9
 * Some points were also retrieved from the 'imx_run.c' program from Andrew Trotman:
 *      http://atose.org/?page_id=215
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

#include "imx_usb_lib.h"

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))

struct sdp_command {
    uint8_t report_id;
    uint16_t command_type;
    uint32_t address;
    uint8_t format;
    uint32_t data_count;
    uint32_t data;
    uint8_t reserved;
} __attribute__((packed));

enum {
    HAB_PRODUCTION,
    HAB_ENGINEERING,
};

/*
 * Constants to do with the IMXIMAGE file foramt
 */
#define IMX_IMAGE_VERSION                  0x40
#define IMX_IMAGE_FILE_HEADER_LENGTH     0x2000
#define IMX_IMAGE_TAG_FILE_HEADER          0xD1

struct imx_image_header
{
    uint8_t tag;               // see IMX_IMAGE_TAG_xxx
    uint16_t length;           // BigEndian format
    uint8_t version;           // for the i.MX6 this should be either 0x40 or 0x41
} __attribute__((packed));

struct imx_image_ivt
{
    struct imx_image_header header;
    uint32_t entry;               // Absolute address of the first instruction to execute from the image
    uint32_t reserved1;
    uint32_t dcd;              // Absolute address of the image DCD. The DCD is optional so this field may be set to NULL if no DCD is required
    uint32_t boot_data;        // Absolute address of the Boot Data
    uint32_t self;             // Absolute address of the IVT
    uint32_t csf;              // Absolute address of Command Sequence File (CSF) used by the HAB library
    uint32_t reserved2;
} __attribute__((packed));

static libusb_context *_context = NULL;

#define SDP_READ_REGISTER 0x0101
#define SDP_WRITE_REGISTER 0x0202
#define SDP_WRITE_FILE 0x0404
#define SDP_ERROR_STATUS 0x0505
#define SDP_DCD_WRITE 0x0a0a
#define SDP_JUMP_ADDRESS 0x0b0b


#define HID_GET_REPORT              0x01
#define HID_SET_REPORT              0x09
#define HID_REPORT_TYPE_INPUT       0x01
#define HID_REPORT_TYPE_OUTPUT      0x02
#define HID_REPORT_TYPE_FEATURE     0x03

#define CTRL_IN                 LIBUSB_ENDPOINT_IN |LIBUSB_REQUEST_TYPE_CLASS|LIBUSB_RECIPIENT_INTERFACE
#define CTRL_OUT                LIBUSB_ENDPOINT_OUT|LIBUSB_REQUEST_TYPE_CLASS|LIBUSB_RECIPIENT_INTERFACE

#define TIMEOUT 1000

#define EP_IN 0x81

static void dump(const char *msg, void *data, int len)
{
    uint8_t *d8 = data;
    int i;

    printf("== %s [%d] ==\n", msg, len);
    for (i = 0; i < len; i++) {
        if ((i % 16) == 0)
            printf("%3.3x:", i);
        printf(" %2.2x", d8[i]);
        if ((i % 16) == 15)
            printf("\n");
    }
    if ((len % 16) != 15)
        printf("\n");
}

static int usb_error(int e, const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	fprintf(stderr, "Error: %d - %s: ", e, libusb_error_name(e));
	vfprintf(stderr, msg, ap);
	fprintf(stderr, "\n");
	return e;
}

void imx_disconnect(struct libusb_device_handle *h)
{
    libusb_release_interface(h, 0);
    libusb_close(h);
}

libusb_device_handle *imx_connect(void)
{
    int count, i;
    libusb_device **devs = NULL;
    libusb_device *dev = NULL;
    libusb_device_handle *h = NULL;

    if (!_context)
        if (libusb_init(&_context) < 0)
            return NULL;

    count = libusb_get_device_list(_context, &devs);
    if (count < 0 || !devs) {
        usb_error(count, "get_device_list");
        return NULL;
    }
    for (i = 0; i < count; i++) {
        int e;
        struct libusb_device_descriptor desc;

        dev = devs[i];
        e = libusb_get_device_descriptor(dev, &desc);
        if (e < 0) {
            usb_error(e, "get_device_descriptor");
            continue;
        }
        if (desc.idVendor == 0x15a2 &&
		(desc.idProduct == 0x0052 ||
		desc.idProduct == 0x0054))
            break;
    }
    if (i == count)
        dev = NULL;

    if (dev) {
        int e;
        e = libusb_open(dev, &h);
        if (e < 0) {
            usb_error(e, "libusb_open /dev/bus/usb/%.3d/%.3d",
	    	libusb_get_bus_number(dev), libusb_get_device_address(dev));
            h = NULL;
        } else {
            if (libusb_kernel_driver_active(h, 0))
                libusb_detach_kernel_driver(h, 0);

            e = libusb_claim_interface(h, 0);
            if (e < 0) {
                usb_error(e, "claim_interface");
                h = NULL;
            }
        }
    }

    if (devs)
        libusb_free_device_list(devs, 1);

    return h;
}

static int hab_type(uint8_t *hab, int len)
{
    if (len < 4) {
        printf("Invalid hab len: %d\n", len);
        return -1;
    }
    if (hab[0] == 0x56 && hab[3] == 0x56 && hab[1] == 0x78 && hab[2] == 0x78)
        return HAB_ENGINEERING;
    if (hab[0] == 0x12 && hab[3] == 0x12 && hab[1] == 0x34 && hab[2] == 0x34)
        return HAB_PRODUCTION;
    return -1;
}

static int imx_send_sdp(libusb_device_handle *h, struct sdp_command *cmd)
{
    int i, e;
    for (i = 5; i; i--) {
        e = libusb_control_transfer(h, CTRL_OUT, HID_SET_REPORT,
                (HID_REPORT_TYPE_OUTPUT << 8) | cmd->report_id,
                0, (void *)cmd, sizeof(struct sdp_command), TIMEOUT);
        if (e >= 0)
            return e;
    }

    return usb_error(e, "sdp libusb_control_transfer");
}

static int imx_read_hab(libusb_device_handle *h)
{
    uint8_t hab[65] = {0};
    int len, e;

    e = libusb_interrupt_transfer(h, EP_IN, hab, sizeof(hab), &len, TIMEOUT);
    if (e < 0)
        return usb_error(e, "libusb_interrupt_transfer HAB");

    if (hab[0] != 3) {
        fprintf(stderr, "Invalid HAB report ID: 0x%x\n", hab[0]);
        return -EINVAL;
    }

    e = hab_type(&hab[1], len - 1);
    if (e < 0)
        return e;

    return 0;
}

static int imx_write_reg(libusb_device_handle *h, uint32_t addr,
		uint32_t data, int count, int format)
{
    int e;
    struct sdp_command cmd = {0};
    uint8_t buffer[65];
    int len;

    //printf("Writing 0x%8.8x to 0x%8.8x\n", data, addr);

    cmd.report_id = 1;
    cmd.command_type = SDP_WRITE_REGISTER;
    cmd.address = htonl(addr);
    cmd.format = format;
    cmd.data_count = htonl(count);
    cmd.data = htonl(data);

    //dump("write_cmd", &cmd, sizeof(cmd));
    e = imx_send_sdp(h, &cmd);
    if (e < 0)
        return e;

    /* Read the HAB data */
    e = imx_read_hab(h);
    if (e < 0)
        return e;

    /* Read the response data */
    memset(buffer, 0, sizeof(buffer));
    buffer[0] = 4;
    len = 0;
    e = libusb_interrupt_transfer(h, EP_IN, buffer, sizeof(buffer), &len,
		    TIMEOUT);
    if (e < 0)
        return usb_error(e, "libusb_interrupt_transfer");

    if (len != 65) {
        fprintf(stderr, "Insufficient write response data: %d\n", len);
        return -EINVAL;
    }
    if (buffer[0] != 4) {
        fprintf(stderr, "Incorrect report type: 0x%x\n", buffer[0]);
        return -EINVAL;
    }
    if (buffer[1] != 0x12 || buffer[2] != 0x8a || buffer[3] != 0x8a ||
        buffer[4] != 0x12) {
        fprintf(stderr, "Invalid write response\n");
        return -EINVAL;
    }
    //dump("write_response", buffer, len);

    return 0;
}

int imx_write_reg32(libusb_device_handle *h, uint32_t addr, uint32_t data)
{
    return imx_write_reg(h, addr, data, 1, 0x20);
}

int imx_write_reg16(libusb_device_handle *h, uint32_t addr, uint16_t data)
{
    return imx_write_reg(h, addr, data, 1, 0x10);
}

int imx_write_reg8(libusb_device_handle *h, uint32_t addr, uint8_t data)
{
    return imx_write_reg(h, addr, data, 1, 0x8);
}

int imx_dcd_write(libusb_device_handle *h, uint32_t *data, int count)
{
    int e;
    struct sdp_command cmd = {0};
    uint8_t dcd_data[1024];
    int len, i;

    if (count > 85) {
        fprintf(stderr, "DCD writes must be < 85 32-bit words\n");
        return -EINVAL;
    }

    /* Convert them all to the required big-endian format */
    for (i = 0; i < count * 3; i++) {
        data[i] = htonl(data[i]);
    }

    //printf("Writing 0x%8.8x to 0x%8.8x\n", data, addr);

    cmd.report_id = 1;
    cmd.command_type = SDP_DCD_WRITE;
    cmd.data_count = htonl(count);

    //dump("write_cmd", &cmd, sizeof(cmd));
    e = imx_send_sdp(h, &cmd);
    if (e < 0)
        return e;

    /* Write the DCD data */
    dcd_data[0] = 2;
    memcpy(&dcd_data[1], data, count * 12);
    //dump("dcd_data", dcd_data, count * 12 + 1);
    e = libusb_control_transfer(h, CTRL_OUT, HID_SET_REPORT,
            (HID_REPORT_TYPE_OUTPUT << 8) | 2,
            0, dcd_data, count * 12 + 1, TIMEOUT);
    if (e < 0)
        return usb_error(e, "libusb_control_transfer dcd");

    /* Read the HAB data */
    e = imx_read_hab(h);
    if (e < 0)
        return e;

    /* Read the response data */
    memset(dcd_data, 0, sizeof(dcd_data));
    dcd_data[0] = 4;
    len = 0;
    e = libusb_interrupt_transfer(h, EP_IN, dcd_data, sizeof(dcd_data),
		    &len, TIMEOUT);
    if (e < 0)
        return usb_error(e, "libusb_interrupt_transfer");

    if (len != 65) {
        fprintf(stderr, "Insufficient write response data: %d\n", len);
        return -EINVAL;
    }
    //dump("dcd_write_response", dcd_data, len);
    if (dcd_data[0] != 4) {
        fprintf(stderr, "Incorrect report type: 0x%x\n", dcd_data[0]);
        return -EINVAL;
    }
    if (dcd_data[1] != 0x12 || dcd_data[2] != 0x8a || dcd_data[3] != 0x8a ||
        dcd_data[4] != 0x12) {
        fprintf(stderr, "Invalid write response\n");
        return -EINVAL;
    }

    return 0;
}

static int imx_write_bulk_block(libusb_device_handle *h, uint32_t addr,
		uint8_t *data, int length)
{
    int e;
    struct sdp_command cmd = {0};
    int len;
    uint8_t write_data[1025];

    if (length > 1024)
        return -EINVAL;

    cmd.report_id = 1;
    cmd.command_type = SDP_WRITE_FILE;
    cmd.address = htonl(addr);
    cmd.data_count = htonl(length);

    //dump("write_cmd", &cmd, sizeof(cmd));
    e = imx_send_sdp(h, &cmd);
    if (e < 0)
        return e;

    write_data[0] = 2;
    memcpy(&write_data[1], data, length);
    e = libusb_control_transfer(h, CTRL_OUT, HID_SET_REPORT,
            (HID_REPORT_TYPE_OUTPUT << 8) | 2,
            0, write_data, length + 1, TIMEOUT);
    if (e < 0)
        return usb_error(e, "libusb_control_transfer write_file");

    /* Read the HAB data */
    e = imx_read_hab(h);
    if (e < 0)
        return e;

    /* Read the response data */
    e = libusb_interrupt_transfer(h, EP_IN, write_data, sizeof(write_data),
		    &len, TIMEOUT);
    if (e < 0)
        return usb_error(e, "libusb_interrupt_transfer");

    if (len != 65) {
        fprintf(stderr, "Insufficient write response data: %d\n", len);
        return -EINVAL;
    }
    //dump("dcd_write_response", write_data, len);
    if (write_data[0] != 4) {
        fprintf(stderr, "Incorrect report type: 0x%x\n", write_data[0]);
        return -EINVAL;
    }
    if (write_data[1] != 0x88 || write_data[2] != 0x88 ||
        write_data[3] != 0x88 || write_data[4] != 0x88) {
        fprintf(stderr, "Invalid write response\n");
        return -EINVAL;
    }

    return 0;
}

int imx_write_bulk(libusb_device_handle *h, uint32_t addr, uint8_t *data,
		int length)
{
    int i;

    for (i = 0; i < length; i+= 1024) {
        int this_len = min(1024, length - i);
        int e;
        e = imx_write_bulk_block(h, addr, data, this_len);
        if (e < 0)
            return e;
        addr += this_len;
        data += this_len;
    }

    return 0;
}

int imx_read_bulk(libusb_device_handle *h, uint32_t addr, uint8_t *result,
        int count, int format)
{
    int e;
    struct sdp_command cmd = {0};
    uint8_t buffer[65];
    int len;
    int remaining = count;

    //printf("Reading %d %d-bit values from 0x%8.8x remaining %d\n", count, format, addr, remaining);
    cmd.report_id = 1;
    cmd.command_type = SDP_READ_REGISTER;
    cmd.address = htonl(addr);
    cmd.format = format;
    cmd.data_count = htonl(count);

    //dump("read_cmd", &cmd, sizeof(cmd));

    e = imx_send_sdp(h, &cmd);
    if (e < 0)
        return e;

    /* Read the HAB data */
    e = imx_read_hab(h);
    if (e < 0)
        return e;

    /* Read the actual data */
    while (remaining) {
        memset(buffer, 0, sizeof(buffer));
        buffer[0] = 4;
        len = 0;
        e = libusb_interrupt_transfer(h, EP_IN, buffer, sizeof(buffer),
			&len, TIMEOUT);
        if (e < 0)
            return usb_error(e, "libusb_interrupt_transfer read resp");

        if (len > 1) {
            int this = min(remaining, len - 1);
            memcpy(result, &buffer[1], this);

            //dump("read_resp", buffer, sizeof(buffer));

            result += this;
            remaining -= this;
        }

    }

    return 0;
}

int imx_read_reg32(libusb_device_handle *h, uint32_t addr, uint32_t *value)
{
    return imx_read_bulk(h, addr, (uint8_t *)value, 4, 0x20);
}

int imx_read_reg16(libusb_device_handle *h, uint32_t addr, uint16_t *value)
{
    return imx_read_bulk(h, addr, (uint8_t *)value, 2, 0x10);
}

int imx_read_reg8(libusb_device_handle *h, uint32_t addr, uint8_t *value)
{
    return imx_read_bulk(h, addr, value, 1, 0x08);
}

/**
 * The IMX is looking for an IMX image, so we create a fake entry
 * for one, and point it's jump address to the one we're after.
 * This means there must always be sizeof(struct imx_image_ivt) memory
 * available before the address passed to this function
 */
int imx_jump_address(libusb_device_handle *h, uint32_t addr)
{
    int e;
    struct sdp_command cmd = {0};
    uint8_t buffer[65];
    int len;
    struct imx_image_ivt fake;

    /* Write a pretend IVT header */
    memset(&fake, 0, sizeof(fake));
    fake.header.tag = IMX_IMAGE_TAG_FILE_HEADER;
    fake.header.length = IMX_IMAGE_FILE_HEADER_LENGTH;
    fake.header.version = IMX_IMAGE_VERSION;
    fake.entry = addr;
    fake.self = addr - sizeof(fake);
    addr = fake.self;
    e = imx_write_bulk(h, addr, (uint8_t *)&fake, sizeof(fake));
    if (e < 0)
        return e;

    cmd.report_id = 1;
    cmd.command_type = SDP_JUMP_ADDRESS;
    cmd.address = htonl(addr);

    e = imx_send_sdp(h, &cmd);
    if (e < 0)
        return e;

    /* Read the HAB data */
    e = imx_read_hab(h);
    if (e < 0)
        return e;

    /* Read the response data - this shouldn't happen */
    memset(buffer, 0, sizeof(buffer));
    buffer[0] = 4;
    len = 0;
    e = libusb_interrupt_transfer(h, EP_IN, buffer, sizeof(buffer), &len, TIMEOUT);
    /* We actually expect USB to fail here, since we've just jumped out
     * of the USB Bootloader code
     */
    if (e < 0)
        return 0;

   fprintf(stderr, "Failure: Continued USB comms after jump\n");
   dump("jump_response", buffer, len);
   return -EINVAL;
}

