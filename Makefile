# Build wrapper around CMake.
#
#   make build                # configure + build (Release)
#   make build BUILD_TYPE=Debug
#   make install              # install to PREFIX (may need sudo)
#   make clean                # remove build/

BUILD_DIR ?= build
BUILD_TYPE ?= Release
PREFIX ?= /usr/local
DESTDIR ?=

.PHONY: all build install clean

all: build

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR) --parallel

install: build
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BUILD_DIR)/lumen $(DESTDIR)$(PREFIX)/bin/lumen

clean:
	rm -rf $(BUILD_DIR)
