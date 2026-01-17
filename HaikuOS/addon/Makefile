# SoftKM Input Server Device Add-on Makefile

NAME = SoftKMDevice
TYPE = SHARED
APP_MIME_SIG =

SRCS = SoftKMDevice.cpp

RDEFS =

LIBS = be

LIBPATHS =

SYSTEM_INCLUDE_PATHS =

LOCAL_INCLUDE_PATHS =

OPTIMIZE := FULL

LOCALES =

DEFINES =

WARNINGS = ALL

SYMBOLS :=

DEBUGGER :=

COMPILER_FLAGS =

LINKER_FLAGS = -Xlinker -soname=$(NAME)

# Determine the CPU type
MACHINE = $(shell uname -m)
ifeq ($(MACHINE), BePC)
	CPU = x86
else
	CPU = $(MACHINE)
endif

# Set the object directory
OBJDIR := objects.$(CPU)-$(if $(DEBUGGER),debug,release)

# Include the Makefile-Engine
BUILDHOME = /boot/system/develop
include $(BUILDHOME)/etc/makefile-engine

# Installation target (use different name to avoid conflict)
addon_install: $(TARGET)
	mkdir -p ~/config/non-packaged/add-ons/input_server/devices
	cp $(TARGET) ~/config/non-packaged/add-ons/input_server/devices/
	@echo "Add-on installed. Restart input_server with: /system/servers/input_server -q"
