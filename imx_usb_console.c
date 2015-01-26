/**
 * \file	imx_usb_console.c
 * \date	2013-Sep-12
 * \author	Andre Renaud
 * \copyright	Aiotec Ltd/Bluewater Systems
 * \brief       Front-end program to provide command line access to i.MX??
 *              via USB bootloader
 */
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "imx_usb_lib.h"
#include "imx_drv_spi.h"
#include "imx_drv_gpio.h"
#include "parser.h"

#define mseconds() (int)({struct timeval _tv; gettimeofday(&_tv, NULL); _tv.tv_sec * 1000 + _tv.tv_usec / 1000; })

static libusb_device_handle *h = NULL;

#define REQUIRE_PARAMS(n) if (argc < n) { fprintf(stderr, "Requires %d params\n", n);  return -EINVAL; }

struct define_rec {
	const char *name;
	const char *value;
};

static struct define_rec **defines = NULL;
static int ndefines = 0;

static uint32_t val2addr(const char *val)
{
	int i;
	for (i = 0; i < ndefines; i++) {
		if (strcmp(val, defines[i]->name) == 0) {
			val = defines[i]->value;
			break;
		}
	}

	if (!isdigit(*val))
		fprintf(stderr, "Invalid addr %s\n", val);

	return strtoul(val, NULL, 0);
}

static int define_func(int argc, char *argv[])
{
	struct define_rec *define;
	REQUIRE_PARAMS(2);
	ndefines++;
	defines = realloc(defines, ndefines * sizeof(struct define_rec *));
	defines[ndefines - 1] = malloc(sizeof(struct define_rec));
	define = defines[ndefines - 1];

	define->name = strdup(argv[1]);
	define->value = strdup(argv[2]);

	//printf("Added define %d %s = %s\n", ndefines - 1, define->name, define->value);

	return 0;
}

static int read_reg32(int argc, char *argv[])
{
    uint32_t value;
    uint32_t addr;
    int e;

    REQUIRE_PARAMS(2);

    addr = val2addr(argv[1]);
    e = imx_read_reg32(h, addr, &value);
    if (e < 0) {
        fprintf(stderr, "Failed to read 0x%8.8x\n", addr);
        return e;
    }
    printf("0x%8.8x = 0x%8.8x\n", addr, value);
    return 0;
}

static int write_reg32(int argc, char *argv[])
{
    uint32_t value;
    uint32_t addr;
    int e;

    REQUIRE_PARAMS(3);
    addr = val2addr(argv[1]);
    value = strtoul(argv[2], NULL, 0);
    e = imx_write_reg32(h, addr, value);
    if (e < 0)
        fprintf(stderr, "Failed to write 0x%8.8x = 0x%8.8x\n",
                addr, value);
    return e;
}

static int write_reg16(int argc, char *argv[])
{
    uint16_t value;
    uint32_t addr;
    int e;

    REQUIRE_PARAMS(3);
    addr = val2addr(argv[1]);
    value = strtoul(argv[2], NULL, 0);
    e = imx_write_reg16(h, addr, value);
    if (e < 0)
        fprintf(stderr, "Failed to write 0x%8.8x = 0x%4.4x\n",
                addr, value);
    return e;
}

static int read_reg16(int argc, char *argv[])
{
    uint16_t value;
    uint32_t addr;
    int e;

    REQUIRE_PARAMS(2);

    addr = val2addr(argv[1]);
    e = imx_read_reg16(h, addr, &value);
    if (e < 0) {
        fprintf(stderr, "Failed to read 0x%8.8x\n", addr);
        return e;
    }
    printf("0x%8.8x = 0x%4.4x\n", addr, value);
    return 0;
}

static int write_reg8(int argc, char *argv[])
{
    uint8_t value;
    uint32_t addr;
    int e;

    REQUIRE_PARAMS(3);
    addr = val2addr(argv[1]);
    value = strtoul(argv[2], NULL, 0);
    e = imx_write_reg8(h, addr, value);
    if (e < 0)
        fprintf(stderr, "Failed to write 0x%8.8x = 0x%2.2x\n",
                addr, value);
    return e;
}

static int read_reg8(int argc, char *argv[])
{
    uint8_t value;
    uint32_t addr;
    int e;

    REQUIRE_PARAMS(2);

    addr = val2addr(argv[1]);
    e = imx_read_reg8(h, addr, &value);
    if (e < 0) {
        fprintf(stderr, "Failed to read 0x%8.8x\n", addr);
        return e;
    }
    printf("0x%8.8x = 0x%2.2x\n", addr, value);
    return 0;
}

static void *buffer_file(const char *file, size_t *file_size)
{
    struct stat file_stat;
    ssize_t bytes;
    FILE *fd;
    char *buffer;

    fd = fopen(file, "r");
    if (!fd)
        return NULL;

    stat(file, &file_stat);
    *file_size = file_stat.st_size;
    buffer = malloc(*file_size);
    if (!buffer) {
        fclose(fd);
        return NULL;
    }

    bytes = fread(buffer, 1, *file_size, fd);
    if (bytes < *file_size) {
        free(buffer);
        fclose(fd);
        return NULL;
    }

    fclose(fd);
    return buffer;
}

static int write_file(int argc, char *argv[])
{
    const char *file;
    uint32_t addr;
    size_t length;
    uint8_t *data;
    int e;
    int start, duration;

    REQUIRE_PARAMS(3);

    addr = val2addr(argv[1]);
    file = argv[2];

    data = buffer_file(file, &length);
    if (!data) {
        perror("buffer file");
        return -EINVAL;
    }

    start = mseconds();
    e = imx_write_bulk(h, addr, data, length);
    free(data);
    if (e < 0)
        fprintf(stderr, "Failed to write %s to 0x%8.8x [%zd bytes]\n",
                file, addr, length);
    duration = mseconds() - start;
    printf("Took %dms to write %zdB: %zdkB/s\n",
        duration, length, ((length / 1024) * 1000) / duration);
    return e;
}

static int verify_file(int argc, char *argv[])
{
    const char *file;
    uint32_t addr;
    size_t length;
    uint8_t *data;
    uint8_t *read_back;
    int e, i;
    int start, duration;

    REQUIRE_PARAMS(3);

    addr = val2addr(argv[1]);
    file = argv[2];

    data = buffer_file(file, &length);
    if (!data) {
        perror("buffer file");
        return -EINVAL;
    }

    read_back = malloc(length);
    if (!read_back) {
        perror("malloc");
        free(data);
        return -ENOMEM;
    }

    start = mseconds();
    e = imx_read_bulk(h, addr, read_back, length, 8);
    if (e < 0)
        fprintf(stderr, "Failed to write %s to 0x%8.8x [%zd bytes]\n",
                file, addr, length);
    duration = mseconds() - start;
    printf("Took %dms to read %zdB: %zdkB/s\n",
        duration, length, ((length / 1024) * 1000) / duration);
    if (e >= 0 && memcmp(read_back, data, length) != 0) {
        for (i = 0; i < length; i++)
            if (read_back[i] != data[i]) {
                printf("Mismatch @ 0x%8.8x: 0x%2.2x != 0x%2.2x\n",
                        addr + i, read_back[i], data[i]);
                break;
            }
        e = -EINVAL;
    }
    free(read_back);
    free(data);
    return e;
}

static int dump_mem32(int argc, char *argv[])
{
    uint32_t addr;
    int length;
    uint32_t *data;
    int i, e;

    REQUIRE_PARAMS(3);

    addr = val2addr(argv[1]);
    length = strtoul(argv[2], NULL, 0);

    data = malloc(length * 4);
    if (!data) {
        perror("malloc");
        return -ENOMEM;
    }

    e = imx_read_bulk(h, addr, (uint8_t *)data, length * 4, 32);
    if (e < 0) {
        free(data);
        return e;
    }

    for (i = 0; i < length; i++) {
        if ((i % 4) == 0)
            printf("%8.8x:", addr + i * 4);
        printf(" %8.8x", data[i]);
        if ((i % 4) == 3)
            printf("\n");
    }
    if ((length % 4) != 0)
        printf("\n");

    free(data);

    return e;
}

static int dump_mem(int argc, char *argv[])
{
    uint32_t addr;
    int length;
    uint8_t *data;
    int i, e;

    REQUIRE_PARAMS(3);

    addr = val2addr(argv[1]);
    length = strtoul(argv[2], NULL, 0);

    data = malloc(length);
    if (!data) {
        perror("malloc");
        return -ENOMEM;
    }

    e = imx_read_bulk(h, addr, data, length, 8);
    if (e < 0) {
        free(data);
        return e;
    }

    for (i = 0; i < length; i++) {
        if ((i % 16) == 0)
            printf("%8.8x:", addr + i);
        printf(" %2.2x", data[i]);
        if ((i % 16) == 15)
            printf("\n");
    }
    if ((length % 16) != 15)
        printf("\n");

    free(data);

    return e;
}

static inline void dump_percentage(int percentage)
{
	static int last_percentage = -1;

	if (percentage != last_percentage) {
		printf("%03d%%\b\b\b\b", percentage);
		last_percentage = percentage;
		fflush(stdout);
	}
}

static int mtest(int argc, char *argv[])
{
	uint32_t start;
	uint32_t len;
	int size;
	int e, i;
	uint32_t value;

	REQUIRE_PARAMS(3);

	start = strtoul(argv[1], NULL, 0);
	len = strtoul(argv[2], NULL, 0);
	if (argc >= 4)
		size = strtoul(argv[3], NULL, 0);
	else
		size = 4; // default to 32-bit access

	printf("Write: ");
	for (i = 0; i < len / size; i+=size) {
		// FIXME: write differently depending on size
		value = i;
		e = imx_write_reg32(h, start + i, value);
		if (e < 0)
			return e;
		dump_percentage((i * 100) / (len / size));
	}
	printf("100%%\n");

	printf("Read: ");
	for (i = 0; i < len / size; i+=size) {
		uint32_t read_val;
		value = i;
		e = imx_read_reg32(h, start + i, &read_val);
		if (e < 0)
			return e;
		if (read_val != value) {
			fprintf(stderr, "Comparison failure @ 0x%08x: 0x%08x != 0x%08x\n",
			start + len, value, read_val);
			return -EINVAL;
		}
		dump_percentage((i * 100) / (len / size));
	}
	printf("100%%\n");
	return 0;
}

static int jump(int argc, char *argv[])
{
    uint32_t addr;
    int e;

    REQUIRE_PARAMS(2);

    addr = val2addr(argv[1]);
    e = imx_jump_address(h, addr);
    if (e < 0)
        fprintf(stderr, "Failed to jump to 0x%8.8x\n", addr);
    else {
        printf("Jumped to 0x%8.8x\n", addr);
        imx_disconnect(h);
        h = NULL;
    }
    return e;
}

static int usleep_func(int argc, char *argv[])
{
	REQUIRE_PARAMS(1);
	usleep(atoi(argv[1]));
	return 0;
}

static uint8_t fromhex(char v)
{
	if (v >= '0' && v <= '9')
		return v - '0';
	if (v >= 'a' && v <= 'f')
		return 10 + (v - 'a');
	if (v >= 'A' && v <= 'F')
		return 10 + (v - 'A');
	return 0;

}

static int spi_func(int argc, char *argv[])
{
	uint32_t dev;
	uint32_t gpio;
	int cs;
	uint8_t rx[128];
	uint8_t tx[128];
	int len;
	int i;
	uint32_t mode = 0; // FIXME: VALUE?

	REQUIRE_PARAMS(5);

	dev = strtoul(argv[1], NULL, 0);
	cs = strtoul(argv[2], NULL, 0);
	gpio = strtoul(argv[3], NULL, 0);
	len = strtoul(argv[4], NULL, 0);
	if (len >= sizeof(tx)) {
		fprintf(stderr, "Maximum SPI transfer length is %ld\n",
				sizeof(tx));
		return -1;
	}

	memset(tx, 0xff, sizeof(tx));
	if (argc >= 6) {
		char *pos = argv[5];
		i = 0;
		while (*pos) {
			tx[i] = fromhex(*pos) << 4;
			pos++;
			if (*pos) {
				tx[i] |= fromhex(*pos);
				pos++;
			}
			i++;
		}
	}

	if (imx_spi_init(h, dev, cs, 20000000, mode) < 0)
		return -1;
	if (imx_spi_xfer(h, dev, gpio, tx, rx, len) < 0)
		return -1;
	for (i = 0; i < len; i++)
		printf("%2.2x", rx[i]);
	printf("\n");
	if (imx_spi_close(h, dev) < 0)
		return -1;
	return 0;
}

static int gpio_func(int argc, char *argv[])
{
	const char *command;
	uint32_t bank;
	uint32_t pin;
	uint32_t gpio;
	int e;

	REQUIRE_PARAMS(4);

	command = argv[1];
	bank = strtoul(argv[2], NULL, 0);
	pin = strtoul(argv[3], NULL, 0);

	gpio = MXC_GPIO(bank, pin);

	if (strcmp(command, "direction") == 0) {
		e = gpio_get_direction(h, gpio);
		if (e >= 0)
			printf("%s\n", e ? "OUT" : "IN");
	} else if (strcmp(command, "set") == 0) {
		e = gpio_set_value(h, gpio, 1);
	} else if (strcmp(command, "clear") == 0) {
		e = gpio_set_value(h, gpio, 0);
	} else if (strcmp(command, "in") == 0) {
		e = gpio_set_direction(h, gpio, 0);
	} else if (strcmp(command, "out") == 0) {
		e = gpio_set_direction(h, gpio, 1);
	} else if (strcmp(command, "value") == 0) {
		e = gpio_get_value(h, gpio);
		if (e >= 0)
			printf("%s\n", e ? "HIGH" : "LOW");
	} else {
		fprintf(stderr, "Invalid gpio command: %s, expecting: in, out, set, clear, value, direction\n", command);
		e = -1;
	}

	return e;
}

static int include_script(int argc, char *argv[]);

struct parser_function functions[] = {
    {"r32", read_reg32},
    {"w32", write_reg32},
    {"w16", write_reg16},
    {"r16", read_reg16},
    {"w8", write_reg8},
    {"r8", read_reg8},
    {"write_file", write_file},
    {"verify_file", verify_file},
    {"usleep", usleep_func},
    //{"save_file", save_file},
    {"dump", dump_mem},
    {"dump32", dump_mem32},
    {"mtest", mtest},
    {"jump", jump},
    {"include", include_script},
    {"spi", spi_func},
    {"gpio", gpio_func},
    {"#define", define_func},
};

#define NFUNCTIONS ((sizeof(functions)) / sizeof(functions[0]))

static int include_script(int argc, char *argv[])
{
    int e;

    REQUIRE_PARAMS(2);

    e = parse_filename(argv[1], 0, functions, NFUNCTIONS);
    return e;
}
int main(int argc, char *argv[])
{
    h = imx_connect();
    if (!h) {
        fprintf(stderr, "No i.MX device found\n");
        return EXIT_FAILURE;
    }

    signal(SIGQUIT, SIG_IGN);

    if (argc >= 2) {
        int i;
        for (i = 1; i < argc; i++)
            parse_filename(argv[i], 0, functions, NFUNCTIONS);
    } else if (isatty(fileno(stdin))) {
        while (h) {
            char *buffer = readline("IMX-USB> ");
            if (buffer && *buffer)
                add_history(buffer);
            parse_line(buffer, functions, NFUNCTIONS);
            free(buffer);
        }
    } else
        parse_file(stdin, 0, functions, NFUNCTIONS);

    if (h)
        imx_disconnect(h);

    return EXIT_SUCCESS;
}

