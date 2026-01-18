# SoftKM Input Server Device Add-ons Makefile
# Builds separate keyboard and mouse add-ons

# Determine the CPU type
MACHINE = $(shell uname -m)
ifeq ($(MACHINE), BePC)
	CPU = x86
else
	CPU = $(MACHINE)
endif

OBJDIR := objects.$(CPU)-release
BUILDHOME = /boot/system/develop

CC = g++
CFLAGS = -O2 -Wall -fPIC
LDFLAGS = -shared -lbe

KEYBOARD_SRC = SoftKMKeyboard.cpp
KEYBOARD_OBJ = $(OBJDIR)/SoftKMKeyboard.o
KEYBOARD_TARGET = $(OBJDIR)/SoftKMKeyboard

MOUSE_SRC = SoftKMMouse.cpp
MOUSE_OBJ = $(OBJDIR)/SoftKMMouse.o
MOUSE_TARGET = $(OBJDIR)/SoftKMMouse

.PHONY: all clean addon_install

all: $(OBJDIR) $(KEYBOARD_TARGET) $(MOUSE_TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(KEYBOARD_OBJ): $(KEYBOARD_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(KEYBOARD_TARGET): $(KEYBOARD_OBJ)
	$(CC) $(LDFLAGS) -Xlinker -soname=SoftKMKeyboard $< -o $@

$(MOUSE_OBJ): $(MOUSE_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(MOUSE_TARGET): $(MOUSE_OBJ)
	$(CC) $(LDFLAGS) -Xlinker -soname=SoftKMMouse $< -o $@

clean:
	rm -rf $(OBJDIR)

addon_install: all
	mkdir -p ~/config/non-packaged/add-ons/input_server/devices
	cp $(KEYBOARD_TARGET) ~/config/non-packaged/add-ons/input_server/devices/
	cp $(MOUSE_TARGET) ~/config/non-packaged/add-ons/input_server/devices/
	@echo "Add-ons installed. Restart input_server with: /system/servers/input_server -q"
