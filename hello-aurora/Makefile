include ../config.mk

C_DEFS += -DFIRMWARE_VERSION=\"$(FIRMWARE_VERSION)\"

# Project Name
TARGET = hello-aurora

# Build Project for Daisy Bootloader
APP_TYPE = BOOT_SRAM

# Sources
CPP_SOURCES = main.cpp

# Aurora BSP header
C_INCLUDES += -I$(AURORA_SDK_DIR)/include/

# Library Locations
LIBDAISY_DIR ?= $(AURORA_SDK_DIR)/libs/libDaisy
DAISYSP_DIR  ?= $(AURORA_SDK_DIR)/libs/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
