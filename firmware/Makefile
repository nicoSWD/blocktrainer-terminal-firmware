# Thin wrapper over PlatformIO, the host tests and the formatter.
#
#   make                build the default env (wt32-sc01-plus)
#   make debug          build with serial logging forced on
#   make safe           build networking only (no display/sound/LED)
#   make test           host render/stress, wifi and OTA tests
#   make format         rewrite sources with clang-format (format-check only reports)
#   make check          format-check + test + build (run before a PR)
#   make clean          remove PlatformIO build output (host-clean for host/)
#
# PlatformIO comes from the local venv by default; use `make PIO=pio` for a system install.

PIO ?= .venv/bin/pio
ENV := wt32-sc01-plus
BIN := .pio/build/$(ENV)/firmware.bin

.PHONY: all build debug safe test format format-check check clean host-clean help require-pio

all: build

require-pio:
	@command -v $(PIO) >/dev/null 2>&1 || { \
	  echo "PlatformIO not found at '$(PIO)'."; \
	  echo "Install it: python3 -m venv .venv && .venv/bin/pip install platformio"; \
	  exit 1; }

build: require-pio
	$(PIO) run -e $(ENV)
	@echo "Output: $(BIN)"

debug: require-pio
	$(PIO) run -e debug

safe: require-pio
	$(PIO) run -e safe

# The host Makefile reads LVGL/ArduinoJson from .pio/libdeps, so a build must have run once.
test:
	$(MAKE) -C host -j8 run
	$(MAKE) -C host wifi-test
	$(MAKE) -C host ota-test

format:
	../scripts/format.sh

format-check:
	../scripts/format.sh --check

check: format-check test build

clean: require-pio
	$(PIO) run -t clean

host-clean:
	$(MAKE) -C host clean

help:
	@sed -n '1,/^$$/s/^# \{0,1\}//p' Makefile
