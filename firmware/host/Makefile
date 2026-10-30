# Host test harness for the LVGL UI (macOS / Linux, clang). Not part of the PlatformIO build.
#
#   make -j8 run        build and run render + stress   (images go to out/)
#   make -j8 render     build and render all screens only
#   make -j8 stress     build and run the ASAN/UBSan stress run only
#   make clean

FW      := $(abspath ..)
LVGL    := $(FW)/.pio/libdeps/wt32-sc01-plus/lvgl
AJSON   := $(FW)/.pio/libdeps/wt32-sc01-plus/ArduinoJson/src
BUILD   := build
OUT     := out
CC      := clang
CXX     := clang++

SAN        := -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer
# firmware sources: any sanitizer finding aborts the run; LVGL itself only reports
SAN_FATAL  := $(SAN) -fno-sanitize-recover=all
DEFS       := -DUI_HOST_TEST -DLV_CONF_INCLUDE_SIMPLE -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
              -DARDUINOJSON_ENABLE_PROGMEM=0 -DARDUINOJSON_ENABLE_STD_STRING=0 \
              -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0
INC        := -Ishim -I$(FW)/include -I$(FW)/src -I$(LVGL) -I$(AJSON)
COMMON     := -g -O1 $(DEFS) $(INC)
# -fno-sanitize=function: LVGL calls its mask callbacks through a generic pointer type (harmless, clang 17+ flags it)
CFLAGS_LV  := $(COMMON) $(SAN) -fno-sanitize=function -std=gnu11 -w
CXXFLAGS   := $(COMMON) $(SAN_FATAL) -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function

LV_DIRS := core draw hal misc widgets font extra/layouts extra/libs/qrcode extra/others extra/themes extra/widgets
LV_SRC  := $(foreach d,$(LV_DIRS),$(shell find $(LVGL)/src/$(d) -name '*.c'))
LV_SRC  += $(wildcard $(LVGL)/src/extra/*.c)
LV_OBJ  := $(patsubst $(LVGL)/%.c,$(BUILD)/lvgl/%.o,$(LV_SRC))

FW_CXX  := $(FW)/src/ui_pages.cpp $(FW)/src/ui_assets.cpp $(FW)/src/ui_menu.cpp $(FW)/src/pages.cpp $(FW)/src/wifi_scan.cpp
FW_OBJ  := $(patsubst $(FW)/src/%.cpp,$(BUILD)/fw/%.o,$(FW_CXX)) $(patsubst $(FW)/src/%.c,$(BUILD)/fw/%.o,$(wildcard $(FW)/src/fonts/*.c))
# host_main.cpp includes ui_core.cpp itself (it needs its statics)
HOST_OBJ := $(BUILD)/host_main.o $(BUILD)/host_stubs.o

BIN := $(BUILD)/host_test

.PHONY: all run render stress clean
all: $(BIN)

$(BIN): $(LV_OBJ) $(FW_OBJ) $(HOST_OBJ)
	@echo LINK $@
	@$(CXX) $(SAN) -o $@ $^

$(BUILD)/lvgl/%.o: $(LVGL)/%.c
	@mkdir -p $(dir $@)
	@echo CC  $(notdir $<)
	@$(CC) $(CFLAGS_LV) -MMD -c $< -o $@

$(BUILD)/fw/%.o: $(FW)/src/%.cpp
	@mkdir -p $(dir $@)
	@echo CXX $(notdir $<)
	@$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(BUILD)/fw/fonts/%.o: $(FW)/src/fonts/%.c
	@mkdir -p $(dir $@)
	@echo CC  $(notdir $<)
	@$(CC) $(CFLAGS_LV) -MMD -c $< -o $@

$(BUILD)/host_main.o: host_main.cpp $(FW)/src/ui_core.cpp
	@mkdir -p $(BUILD)
	@echo CXX $(notdir $<)
	@$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(BUILD)/host_stubs.o: host_stubs.cpp
	@mkdir -p $(BUILD)
	@echo CXX $(notdir $<)
	@$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

export ASAN_OPTIONS ?= detect_leaks=0:abort_on_error=0:strict_string_checks=1:detect_stack_use_after_return=1
export UBSAN_OPTIONS ?= print_stacktrace=1

render: $(BIN)
	@mkdir -p $(OUT)
	$(BIN) render $(OUT)

stress: $(BIN)
	$(BIN) stress

run: $(BIN)
	@mkdir -p $(OUT)
	$(BIN) all $(OUT)

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

# ---------------------------------------------------------------- OTA download test
# The real ota.cpp against a scripted local server (ota_test/server.py): manifest parsing, version
# comparison and the download loop with stalls, trickles, early closes, redirects and errors.
#   make ota-test
OTA_DEFS  := -DHOST_REAL_TASKS -DOTA_STALL_MS=2000 -DURL_UPDATE='g_manifest_url.c_str()' \
             -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_PROGMEM=0 \
             -DARDUINOJSON_ENABLE_STD_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0
OTA_FLAGS := -g -O1 -std=gnu++17 -pthread $(SAN_FATAL) -Wall -Wextra -Wno-unused-parameter \
             $(OTA_DEFS) -include ota_test/test_url.h -Iota_test/shim -Ishim -I$(FW)/include -I$(FW)/src -I$(AJSON)

$(BUILD)/ota_test: $(FW)/src/ota.cpp ota_test/ota_test.cpp ota_test/ota_test_stubs.cpp ota_test/shim/*.h shim/*.h ota_test/test_url.h
	@mkdir -p $(BUILD)
	$(CXX) $(OTA_FLAGS) $(FW)/src/ota.cpp ota_test/ota_test.cpp ota_test/ota_test_stubs.cpp -o $@

.PHONY: ota-test
ota-test: $(BUILD)/ota_test
	./ota_test/run.sh

# ---------------------------------------------------------------- Wi-Fi scan test
# The real wifi_scan.cpp against a scripted radio and a virtual clock (wifi_test/): scans that fail or come
# back empty, a radio that is busy joining, results the driver freed, duplicate and hidden networks, limits.
#   make wifi-test
WIFI_FLAGS := -g -O1 -std=gnu++17 -pthread $(SAN_FATAL) -Wall -Wextra -Wno-unused-parameter -Ishim -I$(FW)/include -I$(FW)/src -Iwifi_test

$(BUILD)/wifi_test: $(FW)/src/wifi_scan.cpp $(FW)/src/wifi_scan.h wifi_test/wifi_test.cpp wifi_test/wifi_test_stubs.cpp wifi_test/wifi_test_radio.h shim/*.h shim/freertos/*.h
	@mkdir -p $(BUILD)
	$(CXX) $(WIFI_FLAGS) $(FW)/src/wifi_scan.cpp wifi_test/wifi_test.cpp wifi_test/wifi_test_stubs.cpp -o $@

.PHONY: wifi-test
wifi-test: $(BUILD)/wifi_test
	./$(BUILD)/wifi_test
