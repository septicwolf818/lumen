# Build wrapper around CMake.
#
#   make build                     # configure + build (Release)
#   make build BUILD_TYPE=Debug
#   make install                   # user: ~/.local, root/sudo: /usr/local
#   make install PREFIX=/opt/lumen # explicit prefix (needs write access or sudo)
#   make uninstall                 # remove binary; asks about config + sessions
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

# Config and session data written by the app (honour XDG vars).
CONFIG_DIR   := $(if $(XDG_CONFIG_HOME),$(XDG_CONFIG_HOME),$(HOME)/.config)/lumen
CONFIG_FILE  := $(CONFIG_DIR)/config.json
DATA_DIR     := $(if $(XDG_DATA_HOME),$(XDG_DATA_HOME),$(HOME)/.local/share)/lumen
SESSIONS_DIR := $(DATA_DIR)/sessions

.PHONY: all build install uninstall clean

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

uninstall:
	@d="$(INSTALL_ROOT)"; \
	while [ ! -e "$$d" ]; do d=$$(dirname "$$d"); done; \
	if [ ! -w "$$d" ]; then \
		echo "error: cannot write to $(INSTALL_ROOT)"; \
		echo "       run 'sudo make uninstall' for a system-wide install,"; \
		echo "       or 'make uninstall' without sudo for ~/.local"; \
		exit 1; \
	fi
	@if [ -e "$(INSTALL_ROOT)/bin/lumen" ]; then \
		rm -f "$(INSTALL_ROOT)/bin/lumen"; \
		echo "removed: $(INSTALL_ROOT)/bin/lumen"; \
		rmdir "$(INSTALL_ROOT)/bin" 2>/dev/null || true; \
	else \
		echo "not installed: $(INSTALL_ROOT)/bin/lumen"; \
	fi
	@if [ -e "$(CONFIG_FILE)" ]; then \
		printf 'remove config file %s too? [y/N] ' "$(CONFIG_FILE)"; \
		read -r answer || answer=; \
		case "$$answer" in \
			[yY]|[yY][eE][sS]) \
				rm -f "$(CONFIG_FILE)"; \
				rmdir "$(CONFIG_DIR)" 2>/dev/null || true; \
				echo "removed: $(CONFIG_FILE)" ;; \
			*) echo "kept: $(CONFIG_FILE)" ;; \
		esac; \
	fi
	@if [ -e "$(SESSIONS_DIR)" ]; then \
		printf 'remove saved sessions %s too? [y/N] ' "$(SESSIONS_DIR)"; \
		read -r answer || answer=; \
		case "$$answer" in \
			[yY]|[yY][eE][sS]) \
				rm -rf "$(SESSIONS_DIR)"; \
				rmdir "$(DATA_DIR)" 2>/dev/null || true; \
				echo "removed: $(SESSIONS_DIR)" ;; \
			*) echo "kept: $(SESSIONS_DIR)" ;; \
		esac; \
	fi

clean:
	rm -rf $(BUILD_DIR)
