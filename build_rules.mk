#
# IXM_USB_CONSOLE build environment rules
#
# Copyright (c) 2012 Aiotec/Bluewater Systems
#
# Figure out where we are in the filesystem. Sub-directories should override
# BASE to point to the top-level directory before including this file.
#
BASE ?= .
BASE_DIR  = $(shell readlink -f $(BASE))

# Include optional user config file
-include $(BASE_DIR)/config.mk

# What arch are we building for
ifeq "$(ARCH)" ""
	ARCH=arm
endif

# Package builder and development filesystem
PACKAGE_BUILDER	?= $(BASE_DIR)/package-builder
PBUILD		?= $(PACKAGE_BUILDER)/pbuild
DEVELOPMENTFS	?= $(BASE_DIR)/$(ARCH)/developmentfs
PRODUCTIONFS	?= $(BASE_DIR)/$(ARCH)/productionfs

# Third party package support (not needed on i686 builds)
PACKAGE_DIR ?= $(BASE_DIR)/$(ARCH)/packages

# Default flags
CFLAGS	+= -O3
CFLAGS  += -g -Wall -pipe
LFLAGS	+= -L$(DEVELOPMENTFS)/lib
CFLAGS	+= -I$(DEVELOPMENTFS)/include
CFLAGS  += -I$(BASE_DIR)

# Default to no executable suffix
EXESUFFIX =

# Default ARM configuration
ifeq "$(ARCH)" "arm"
	LFLAGS += -mcpu=cortex-a8
	CFLAGS += -DCONFIG_ARM -mcpu=cortex-a8
	CROSS  ?= /tools/arm/gcc-linaro-arm-linux-gnueabihf-4.9-2014.05_linux/bin/arm-linux-gnueabihf-
        PKG_CONFIG ?= $(PACKAGE_BUILDER)/pkg-config
endif

# Default i686 configuration
ifeq "$(ARCH)" "i686"
	X86=y
endif
ifeq "$(ARCH)" "x86_64"
	X86=y
endif

ifeq "$(X86)" "y"
	CFLAGS += -DCONFIG_X86
endif

# Default win32 configuration
ifeq "$(ARCH)" "win32"
	CROSS ?= i586-mingw32msvc-
	EXESUFFIX = .exe
	CFLAGS+=-DCONFIG_WIN32
endif

ifeq "$(PROFILE)" "yes"
	CFLAGS+=-pg -fprofile-arcs -ftest-coverage
       	LFLAGS+=-pg -fprofile-arcs -ftest-coverage
endif 

ifneq "$(DEVELOPMENT)" ""
CFLAGS += -DDEVELOPMENT
endif

# Toolchain programs
CC    = $(CROSS)gcc
CPP   = $(CROSS)g++
CXX   = $(CROSS)g++
AR    = $(CROSS)ar
LD    = $(CROSS)ld
STRIP = $(CROSS)strip
OBJDUMP=$(CROSS)objdump
OBJCOPY=$(CROSS)objcopy
PKG_CONFIG ?= pkg-config

# use C=1 to enable static code checking
ifeq "$(C)" "1"
check_source = echo "  CPPCHECK $1..." ; \
	       cppcheck --quiet --std=c99 $1 ; \
               echo "  SPARSE $1..." ; \
	       sparse $(CFLAGS) -I$(shell $(CC) -print-file-name=include) -I$(shell $(CC) -print-sysroot)/usr/include -I$(shell $(CC) -print-sysroot)/usr/include/linux -Wno-address-space $1
ifneq "$(SMATCH)" ""
check_source += ; echo "  SMATCH $1..." ; \
               $(SMATCH) --two-passes $(CFLAGS) $1
endif
else
check_source =
endif

# Utility macro to generate the dependencies for a given C file
# Arguments should be: C source file, Output filename. 
# It assumes that the .o file is in the same location as the output 
# dependency file
gen_dep = echo "  DEP $1..." ;      \
	  mkdir -p $(dir $2) ;      \
	  $(CC) $1 $(CFLAGS) -MM -MG -MT $(basename $2).o -o $2


# Default directory for object/binary files
ODIR      = obj/$(ARCH)

# Verbose output?
V?=0
ifeq "$(V)" "0"
MAKEFLAGS=--no-print-directory --quiet
endif
