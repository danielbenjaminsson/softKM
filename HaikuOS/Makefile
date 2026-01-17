# softKM Haiku Application Makefile

NAME = softKM
TYPE = APP
APP_MIME_SIG = application/x-vnd.softKM

SRCS = \
	src/main.cpp \
	src/SoftKMApp.cpp \
	src/ui/DeskbarReplicant.cpp \
	src/ui/SettingsWindow.cpp \
	src/network/NetworkServer.cpp \
	src/network/Protocol.cpp \
	src/input/InputInjector.cpp \
	src/settings/Settings.cpp

RDEFS = resources/SoftKM.rdef

LIBS = be network bnetapi tracker game

LIBPATHS =

SYSTEM_INCLUDE_PATHS =

LOCAL_INCLUDE_PATHS = src

OPTIMIZE := FULL

LOCALES =

DEFINES =

WARNINGS = ALL

SYMBOLS :=

DEBUGGER :=

COMPILER_FLAGS =

LINKER_FLAGS =

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
# Build trigger
