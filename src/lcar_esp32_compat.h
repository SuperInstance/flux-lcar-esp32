/**
 * ESP32 / Arduino / ESP-IDF compatibility shim for flux-lcar-esp32.
 * 
 * On ESP32: include this instead of lcar.h directly.
 * Uses ESP-IDF timers for combat ticks, GPIO for gauge inputs,
 * UART for serial command interface.
 * 
 * Works on: ESP32, ESP32-S2, ESP32-S3, ESP32-C3
 */

#ifndef LCAR_ESP32_COMPAT_H
#define LCAR_ESP32_COMPAT_H

#include "lcar.h"

// ═══════════════════════════════════════════
// ESP-IDF or Arduino detection
// ═══════════════════════════════════════════

#ifdef ESP_PLATFORM
  // ESP-IDF
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "driver/uart.h"
  #include "driver/gpio.h"
  #include "esp_timer.h"
  #define LCAR_PLATFORM_ESPIDF
#elif defined(ARDUINO)
  // Arduino IDE
  #include <Arduino.h>
  #define LCAR_PLATFORM_ARDUINO
#else
  // Desktop test
  #define LCAR_PLATFORM_DESKTOP
#endif

// ═══════════════════════════════════════════
// Platform-agnostic abstractions
// ═══════════════════════════════════════════

typedef void (*lcar_tick_fn)(LcarState *state);

// Initialize platform hardware
void lcar_platform_init(void);

// Read a gauge from hardware (ADC, I2C, UART sensor)
int16_t lcar_read_sensor(int gauge_id);

// Send response to connected client (UART, Telnet, or serial)
void lcar_send_response(const char *buf, uint16_t len);

// Start periodic combat tick timer
void lcar_start_ticks(LcarState *state, lcar_tick_fn callback, uint32_t interval_ms);

// Stop combat tick timer
void lcar_stop_ticks(void);

// ═══════════════════════════════════════════
// ESP-IDF Implementation
// ═══════════════════════════════════════════

#ifdef LCAR_PLATFORM_ESPIDF

#define LCAR_UART_NUM      UART_NUM_0
#define LCAR_UART_BUF      256
#define LCAR_UART_BAUD     115200

static esp_timer_handle_t lcar_timer;
static volatile int lcar_gauge_adcs[LCAR_MAX_GAUGES] = {36, 39, 34, 35}; // Default GPIO pins

void lcar_platform_init(void) {
    // Configure UART for command interface
    uart_config_t cfg = {
        .baud_rate = LCAR_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(LCAR_UART_NUM, &cfg);
    uart_driver_install(LCAR_UART_NUM, LCAR_UART_BUF, LCAR_UART_BUF, 0, NULL, 0);
}

int16_t lcar_read_sensor(int gauge_id) {
    // On real hardware: read ADC from GPIO pin
    // Placeholder returns fixed value for now
    // Real implementation: adc1_get_raw(lcar_gauge_adcs[gauge_id])
    return 0;
}

void lcar_send_response(const char *buf, uint16_t len) {
    uart_write_bytes(LCAR_UART_NUM, buf, len);
}

static void lcar_timer_callback(void *arg) {
    LcarState *state = (LcarState *)arg;
    // Read all gauges from real sensors
    for (int r = 0; r < state->room_count; r++) {
        LcarRoom *room = &state->rooms[r];
        for (int g = 0; g < room->gauge_count; g++) {
            room->gauges[g].value = lcar_read_sensor(g);
        }
    }
    state->tick_count++;
}

void lcar_start_ticks(LcarState *state, lcar_tick_fn callback, uint32_t interval_ms) {
    esp_timer_create_args_t args = {
        .callback = lcar_timer_callback,
        .arg = state,
        .name = "lcar_tick",
    };
    esp_timer_create(&args, &lcar_timer);
    esp_timer_start_periodic(lcar_timer, interval_ms * 1000);
}

void lcar_stop_ticks(void) {
    esp_timer_stop(lcar_timer);
    esp_timer_delete(lcar_timer);
}

// ═══════════════════════════════════════════
// Arduino Implementation
// ═══════════════════════════════════════════

#elif defined(LCAR_PLATFORM_ARDUINO)

void lcar_platform_init(void) {
    Serial.begin(115200);
}

int16_t lcar_read_sensor(int gauge_id) {
    // Read from analog pins A0-A3
    int pins[] = {A0, A1, A2, A3};
    if (gauge_id < 4) {
        return (int16_t)(analogRead(pins[gauge_id]) / 4);  // 12-bit to ~8-bit
    }
    return 0;
}

void lcar_send_response(const char *buf, uint16_t len) {
    Serial.write((const uint8_t *)buf, len);
}

static LcarState *arduino_state = NULL;
static lcar_tick_fn arduino_callback = NULL;
static unsigned long arduino_last_tick = 0;
static uint32_t arduino_interval = 1000;

void lcar_start_ticks(LcarState *state, lcar_tick_fn cb, uint32_t interval_ms) {
    arduino_state = state;
    arduino_callback = cb;
    arduino_interval = interval_ms;
    arduino_last_tick = millis();
}

void lcar_stop_ticks(void) {
    arduino_state = NULL;
}

// Call this in loop()
void lcar_arduino_loop(void) {
    if (!arduino_state) return;
    if (millis() - arduino_last_tick >= arduino_interval) {
        arduino_last_tick = millis();
        if (arduino_callback) arduino_callback(arduino_state);
    }
}

#endif // Platform

#endif // LCAR_ESP32_COMPAT_H
