# SoftKMClientFilter — BInputServerFilter add-on Makefile

MACHINE = $(shell uname -m)
ifeq ($(MACHINE), BePC)
	CPU = x86
else
	CPU = $(MACHINE)
endif

OBJDIR := objects.$(CPU)-release
TARGET  = $(OBJDIR)/SoftKMClientFilter

CC      = g++
CFLAGS  = -O2 -Wall -fPIC
LDFLAGS = -shared -lbe -lgame

SRC = SoftKMClientFilter.cpp
OBJ = $(OBJDIR)/SoftKMClientFilter.o

FILTER_DIR = $(HOME)/config/non-packaged/add-ons/input_server/filters

.PHONY: all clean install

all: $(OBJDIR) $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -Xlinker -soname=SoftKMClientFilter $< -o $@

install: all
	mkdir -p $(FILTER_DIR)
	cp $(TARGET) $(FILTER_DIR)/
	@echo "Filter installed to $(FILTER_DIR)"
	@echo "Restart input_server: /system/servers/input_server -q"

clean:
	rm -rf $(OBJDIR)
