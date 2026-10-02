# Build wrapper around CMake.
#
#   make build                     # configure + build (Release)
#   make build BUILD_TYPE=Debug
#   make install                   # user: ~/.local, root/sudo: /usr/local
#   make install PREFIX=/opt/lumen # explicit prefix (needs write access or sudo)
#   make clean                     # remove build/

BUILD_DIR ?= build
BUILD_TYPE ?= Release
DESTDIR ?=

# Default prefix: system-wide when run as root (sudo make install),
# per-user otherwise so no super user is required.
ifeq ($(origin PREFIX), undefined)
  ifeq ($(shell id -u), 0)
    PREFIX := /usr/local
  else
    PREFIX := $(HOME)/.local
  endif
endif

INSTALL_ROOT := $(DESTDIR)$(PREFIX)

.PHONY: all build install clean

all: build

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR)

install: build
	@d="$(INSTALL_ROOT)"; \
	while [ ! -e "$$d" ]; do d=$$(dirname "$$d"); done; \
	if [ ! -w "$$d" ]; then \
		echo "error: cannot write to $(INSTALL_ROOT)"; \
		echo "       run 'sudo make install' for a system-wide install,"; \
		echo "       or 'make install' without sudo for ~/.local"; \
		exit 1; \
	fi
	install -d $(INSTALL_ROOT)/bin
	install -m 755 $(BUILD_DIR)/lumen $(INSTALL_ROOT)/bin/lumen
	@echo "installed: $(INSTALL_ROOT)/bin/lumen"
	@if ! printf '%s' ":$$PATH:" | grep -q ":$(INSTALL_ROOT)/bin:"; then \
		echo "note: $(INSTALL_ROOT)/bin is not in your PATH"; \
	fi

clean:
	rm -rf $(BUILD_DIR)
