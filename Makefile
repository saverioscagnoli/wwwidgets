CC      ?= cc
CFLAGS  ?= -O2 -g
PKGS    := gtk4-layer-shell-0 gtk4 webkitgtk-6.0 json-glib-1.0 wayland-client libpulse libpulse-mainloop-glib
WL_XML  := $(shell pkg-config --variable=pkgdatadir wayland-protocols)/staging/ext-workspace/ext-workspace-v1.xml
CFLAGS  += -MMD -MP -Wall -Wextra -Ibuild -DG_LOG_DOMAIN=\"wwwidgets\" -DG_LOG_USE_STRUCTURED $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))


TARGET  := wwwidgets
PREFIX  ?= $(HOME)/.local
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o) build/resources.o
OBJ     += build/ext-workspace-v1-protocol.o
TL_XML  := protocol/wlr-foreign-toplevel-management-unstable-v1.xml
TL_NAME := wlr-foreign-toplevel-management-unstable-v1
OBJ     += build/$(TL_NAME)-protocol.o

RES_XML := web/resources.gresource.xml
RES_DEP := $(shell glib-compile-resources --sourcedir web --generate-dependencies $(RES_XML))

all: $(TARGET) compile_flags.txt

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/resources.c: $(RES_XML) $(RES_DEP) | build
	glib-compile-resources --sourcedir web --generate-source --target $@ $<

build/resources.o: build/resources.c
	$(CC) $(CFLAGS) -c -o $@ $<

build/ext-workspace-v1-client-protocol.h: $(WL_XML) | build
	wayland-scanner client-header $< $@

build/ext-workspace-v1-protocol.c: $(WL_XML) | build
	wayland-scanner private-code $< $@

build/ext-workspace-v1-protocol.o: build/ext-workspace-v1-protocol.c
	$(CC) $(CFLAGS) -c -o $@ $<

build/workspace.o: build/ext-workspace-v1-client-protocol.h

build/$(TL_NAME)-client-protocol.h: $(TL_XML) | build
	wayland-scanner client-header $< $@

build/$(TL_NAME)-protocol.c: $(TL_XML) | build
	wayland-scanner private-code $< $@

build/$(TL_NAME)-protocol.o: build/$(TL_NAME)-protocol.c
	$(CC) $(CFLAGS) -c -o $@ $<

build/toplevel.o: build/$(TL_NAME)-client-protocol.h

build:
	mkdir -p build

# clangd (Zed, VS Code, ...) reads this to find the GTK/WebKit headers
compile_flags.txt: Makefile
	printf '%s\n' -xc -std=gnu11 -Wall -Wextra -Ibuild $(shell pkg-config --cflags $(PKGS)) > $@

run: all
	./$(TARGET)

release:
	$(MAKE) clean
	CFLAGS="-O2 -DNDEBUG" LDFLAGS="-s" $(MAKE) all

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -rf build $(TARGET) compile_flags.txt

.PHONY: all run release install uninstall clean

-include $(OBJ:.o=.d)
