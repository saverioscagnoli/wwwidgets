MODE    ?= debug
BUILD   := build/$(MODE)
TARGET  := wwwidgets
PREFIX  ?= $(HOME)/.local

ifeq ($(MODE),release)
CFLAGS  ?= -O2 -DNDEBUG
LDFLAGS ?= -s
else
CFLAGS  ?= -Og -g
endif

PKGS    := gtk4-layer-shell-0 gtk4 webkitgtk-6.0 json-glib-1.0 wayland-client libpulse libpulse-mainloop-glib gio-unix-2.0
WP_DIR  := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
CFLAGS  += -MMD -MP -Wall -Wextra -I$(BUILD) -DG_LOG_DOMAIN=\"wwwidgets\" -DG_LOG_USE_STRUCTURED $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))

PROTOS  := $(WP_DIR)/staging/ext-workspace/ext-workspace-v1.xml \
           $(WP_DIR)/staging/ext-data-control/ext-data-control-v1.xml \
           protocol/wlr-foreign-toplevel-management-unstable-v1.xml
NAMES   := $(basename $(notdir $(PROTOS)))
HDRS    := $(NAMES:%=$(BUILD)/%-client-protocol.h)

SRC_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(wildcard src/*.c))
OBJ     := $(SRC_OBJ) $(BUILD)/types.o $(NAMES:%=$(BUILD)/%-protocol.o)

vpath %.xml $(sort $(dir $(PROTOS)))

all: $(BUILD)/$(TARGET) compile_flags.txt

$(BUILD)/$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(SRC_OBJ): | $(HDRS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(BUILD)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%-client-protocol.h: %.xml | $(BUILD)
	wayland-scanner client-header $< $@

$(BUILD)/%-protocol.c: %.xml | $(BUILD)
	wayland-scanner private-code $< $@

$(BUILD)/types.c: web/wwwidgets.d.ts | $(BUILD)
	{ printf 'const char TYPES[] = {'; od -An -v -tx1 $< | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g'; printf '0};\n'; } > $@

$(BUILD):
	mkdir -p $@

compile_flags.txt: Makefile
	printf '%s\n' -xc -std=gnu11 -Wall -Wextra -Ibuild/debug $(shell pkg-config --cflags $(PKGS)) > $@

run: all
	./$(BUILD)/$(TARGET)

install:
	$(MAKE) MODE=release
	install -Dm755 build/release/$(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -rf build $(TARGET) compile_flags.txt

.PHONY: all run install uninstall clean
.SECONDARY:

-include $(OBJ:.o=.d)
