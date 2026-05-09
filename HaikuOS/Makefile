# softKM Haiku Application Makefile

NAME = softKM
TYPE = APP
APP_MIME_SIG = application/x-vnd.softKM

SRCS = \
	src/main.cpp \
	src/SoftKMApp.cpp \
	src/common/clipboard/ClipboardManager.cpp \
	src/common/network/Protocol.cpp \
	src/common/settings/Settings.cpp \
	src/common/ui/DeskbarReplicant.cpp \
	src/common/ui/LogWindow.cpp \
	src/common/ui/SettingsWindow.cpp \
	src/server/network/NetworkServer.cpp \
	src/server/input/InputInjector.cpp \
	src/server/ui/TeamMonitorWindow.cpp \
	src/server/ui/TeamListItem.cpp

RDEFS = resources/SoftKM.rdef

LIBS = be network bnetapi tracker game shared localestub stdc++

LIBPATHS =

SYSTEM_INCLUDE_PATHS = /boot/system/develop/headers/private /boot/system/develop/headers/private/tracker /boot/system/develop/headers/private/shared /boot/system/develop/headers/private/support

# Add every directory that contains headers so unqualified
# `#include "Foo.h"` works regardless of which subtree the includer
# lives in. The Haiku makefile-engine turns these into both -I and
# -iquote flags.
LOCAL_INCLUDE_PATHS = \
	src \
	src/common \
	src/common/clipboard \
	src/common/network \
	src/common/settings \
	src/common/ui \
	src/server \
	src/server/network \
	src/server/input \
	src/server/ui

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
