# dji-wisprer — build the bridge and the discovery tool.
CC      := clang
CFLAGS  := -Wall -O2
FRAMEWORKS_BRIDGE  := -framework IOKit -framework CoreFoundation -framework ApplicationServices
FRAMEWORKS_MONITOR := -framework IOKit -framework CoreFoundation

BUILD := build

.PHONY: all clean install uninstall

all: $(BUILD)/dji-wisprer $(BUILD)/hid-monitor

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/dji-wisprer: src/dji-wisprer.c | $(BUILD)
	$(CC) $(CFLAGS) $(FRAMEWORKS_BRIDGE) $< -o $@

$(BUILD)/hid-monitor: src/hid-monitor.c | $(BUILD)
	$(CC) $(CFLAGS) $(FRAMEWORKS_MONITOR) $< -o $@

clean:
	rm -rf $(BUILD)

install: all
	./install.sh

uninstall:
	./uninstall.sh
