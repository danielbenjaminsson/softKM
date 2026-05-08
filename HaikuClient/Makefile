# softKMClient Haiku Application Makefile
# Haiku-to-Haiku keyboard/mouse sender

NAME = softKMClient
TYPE = APP
APP_MIME_SIG = application/x-vnd.softKMClient

SRCS = \
	src/main.cpp \
	src/SoftKMClientApp.cpp \
	src/ui/DeskbarReplicant.cpp \
	src/ui/SettingsWindow.cpp \
	src/ui/LogWindow.cpp \
	src/network/NetworkClient.cpp \
	src/input/SwitchController.cpp \
	src/clipboard/ClipboardManager.cpp \
	src/settings/Settings.cpp

RDEFS = resources/SoftKMClient.rdef

# Same libs as the server app; 'network' for sockets, 'tracker' for deskbar
LIBS = be network bnetapi tracker game shared localestub stdc++

LIBPATHS =

SYSTEM_INCLUDE_PATHS = \
	/boot/system/develop/headers/private \
	/boot/system/develop/headers/private/tracker \
	/boot/system/develop/headers/private/shared \
	/boot/system/develop/headers/private/support

LOCAL_INCLUDE_PATHS = src

OPTIMIZE := FULL

LOCALES =

DEFINES =

WARNINGS = ALL

SYMBOLS :=

DEBUGGER :=

COMPILER_FLAGS =

LINKER_FLAGS =

MACHINE = $(shell uname -m)
ifeq ($(MACHINE), BePC)
	CPU = x86
else
	CPU = $(MACHINE)
endif

OBJDIR := objects.$(CPU)-$(if $(DEBUGGER),debug,release)

BUILDHOME = /boot/system/develop
include $(BUILDHOME)/etc/makefile-engine
