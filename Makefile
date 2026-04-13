CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -DLCAR_STANDALONE -Isrc

all: lcar_esp32

lcar_esp32: src/lcar.h src/main.c
	$(CC) $(CFLAGS) -o $@ src/main.c

test: lcar_esp32
	./lcar_esp32

clean:
	rm -f lcar_esp32

.PHONY: all test clean

# ESP-IDF build (requires idf.py in PATH)
esp-idf:
	@echo "Building for ESP-IDF..."
	@mkdir -p build/esp
	@cp src/lcar.h build/esp/
	@cp src/lcar_esp32_compat.h build/esp/
	@echo "Copy build/esp/ into your ESP-IDF project components/ directory"

# Arduino build info
arduino:
	@echo "For Arduino:"
	@echo "  1. Copy src/lcar.h to your sketch folder"
	@echo "  2. Copy src/lcar_esp32_compat.h to your sketch folder"
	@echo "  3. Open examples/arduino/flux_lcar_bot/ in Arduino IDE"
	@echo "  4. Select ESP32 board, upload"
