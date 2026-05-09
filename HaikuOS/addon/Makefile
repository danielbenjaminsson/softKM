# softKM input_server add-ons Makefile
#
# Builds three add-ons used by the unified softKM binary depending on
# the configured mode:
#
#   Server mode (receiver):
#     SoftKMKeyboard  — BInputServerDevice that injects synthetic
#                       B_KEY_DOWN/UP events into input_server.
#     SoftKMMouse     — BInputServerDevice that injects synthetic
#                       B_MOUSE_MOVED/DOWN/UP/WHEEL events.
#
#   Client mode (sender):
#     SoftKMClientFilter — BInputServerFilter that captures local
#                          mouse and keyboard events and forwards
#                          them to the softKM application via a
#                          named port.
#
# 'make all' builds all three.  Install targets put each in the
# correct subdir of ~/config/non-packaged/add-ons/input_server/
# (devices/ for the two server-side ones, filters/ for the
# client-side one).  The CI deploy step installs only the add-ons
# relevant to the configured mode.

MACHINE = $(shell uname -m)
ifeq ($(MACHINE), BePC)
	CPU = x86
else
	CPU = $(MACHINE)
endif

OBJDIR := objects.$(CPU)-release
BUILDHOME = /boot/system/develop

CC      = g++
CFLAGS  = -O2 -Wall -fPIC
LDFLAGS = -shared -lbe

# The filter uses set_mouse_position from the game kit, so it needs
# -lgame as well.
FILTER_LDFLAGS = -shared -lbe -lgame

# ---- Server-side device add-ons ----
KEYBOARD_TARGET = $(OBJDIR)/SoftKMKeyboard
MOUSE_TARGET    = $(OBJDIR)/SoftKMMouse

# ---- Client-side filter add-on ----
FILTER_TARGET   = $(OBJDIR)/SoftKMClientFilter

# Install destinations
DEVICES_DIR = $(HOME)/config/non-packaged/add-ons/input_server/devices
FILTERS_DIR = $(HOME)/config/non-packaged/add-ons/input_server/filters

.PHONY: all clean install install_server install_client

all: $(OBJDIR) $(KEYBOARD_TARGET) $(MOUSE_TARGET) $(FILTER_TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Generic compile rule
$(OBJDIR)/%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

# Link rules — name each target after its .o so make does the right
# thing for both server and client variants.
$(KEYBOARD_TARGET): $(OBJDIR)/SoftKMKeyboard.o
	$(CC) $(LDFLAGS) -Xlinker -soname=SoftKMKeyboard $< -o $@

$(MOUSE_TARGET): $(OBJDIR)/SoftKMMouse.o
	$(CC) $(LDFLAGS) -Xlinker -soname=SoftKMMouse $< -o $@

$(FILTER_TARGET): $(OBJDIR)/SoftKMClientFilter.o
	$(CC) $(FILTER_LDFLAGS) -Xlinker -soname=SoftKMClientFilter $< -o $@

# Install only the server-side add-ons (use when softKM is configured
# as MODE_SERVER on the target).
install_server: $(KEYBOARD_TARGET) $(MOUSE_TARGET)
	mkdir -p $(DEVICES_DIR)
	cp $(KEYBOARD_TARGET) $(DEVICES_DIR)/
	cp $(MOUSE_TARGET)    $(DEVICES_DIR)/
	@echo "Server add-ons installed to $(DEVICES_DIR)"
	@echo "Restart input_server: /system/servers/input_server -q"

# Install only the client-side add-on (use when softKM is configured
# as MODE_CLIENT on the target).
install_client: $(FILTER_TARGET)
	mkdir -p $(FILTERS_DIR)
	cp $(FILTER_TARGET) $(FILTERS_DIR)/
	@echo "Client filter installed to $(FILTERS_DIR)"
	@echo "Restart input_server: /system/servers/input_server -q"

# Install both — useful only on a development machine that's testing
# both roles. Production targets pick one or the other.
install: install_server install_client

clean:
	rm -rf $(OBJDIR)
