CC      ?= cc
CFLAGS  ?= -O2 -g
PKGS    := gtk4-layer-shell-0 gtk4 webkitgtk-6.0 json-glib-1.0
CFLAGS  += -MMD -MP -Wall -Wextra -DG_LOG_DOMAIN=\"wwwidgets\" -DG_LOG_USE_STRUCTURED $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))

TARGET  := wwwidgets
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o) build/resources.o

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

build:
	mkdir -p build

# clangd (Zed, VS Code, ...) reads this to find the GTK/WebKit headers
compile_flags.txt: Makefile
	printf '%s\n' -xc -std=gnu11 -Wall -Wextra $(shell pkg-config --cflags $(PKGS)) > $@

run: all
	./$(TARGET)

clean:
	rm -rf build $(TARGET) compile_flags.txt

.PHONY: all run clean

-include $(OBJ:.o=.d)
