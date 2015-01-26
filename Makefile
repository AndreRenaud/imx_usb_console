BASE=.
include $(BASE)/build_rules.mk

CFLAGS += `$(PKG_CONFIG) --cflags libusb-1.0`
LFLAGS += `$(PKG_CONFIG) --libs libusb-1.0`
LFLAGS += -lreadline

SOURCES=imx_usb_lib.c imx_usb_console.c parser.c imx_drv_gpio.c imx_drv_spi.c
OBJECTS=$(patsubst %.c,$(ODIR)/%.o, $(SOURCES))

default: $(ODIR)/imx_usb_console

$(ODIR)/%.o : %.c
	echo "  CC $<..."
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS)

$(ODIR)/imx_usb_console: $(OBJECTS)
	echo "  LD $@..."
	$(CC) -o $@ $(OBJECTS) $(LFLAGS)

clean:
	rm -rf $(ODIR)

