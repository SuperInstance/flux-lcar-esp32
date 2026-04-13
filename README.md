# flux-lcar-esp32 — Bare Metal MUD Interpreter

The quiet interpreter. Fits in **~1.2KB RAM**. Zero malloc. Runs on anything.

Same rooms, gauges, commands as the full FLUX-LCAR server, but stripped to the metal.
Works on ESP32, STM32, Arduino — anything with a UART and 1KB free RAM.

## What It Teaches

What IS the MUD at the absolute minimum? A room array. A gauge struct. A switch statement.
No dynamic allocation. No string formatting beyond snprintf. No JSON. No networking.
Just enough to be spatially legible while being bulletproof.

## Build (desktop test)

```bash
make
make test
```

## Memory Footprint

```
LcarState: ~1200 bytes (16 rooms, 4 agents, all gauges)
Zero malloc. All static. Stack-safe.
```

## Binary Commands

No text parsing — 1-byte opcodes:

| Opcode | Hex | Args | Description |
|--------|-----|------|-------------|
| LOOK | 0x01 | none | See current room |
| GO | 0x02 | exit name | Move to adjacent room |
| SAY | 0x03 | message | Room-local message |
| GAUGE | 0x0A | room, gauge, value | Update a gauge |
| TICK | 0x13 | none | Run one combat tick |
| SIM_MODE | 0x20 | none | Switch to simulation data |
| REAL_MODE | 0x21 | none | Switch to real sensors |
| ALERT | 0x0F | level | Set alert level |
| STATUS | 0x10 | none | Ship status |
| HELP | 0x06 | none | Command list |
| QUIT | 0x07 | none | Disconnect |

## On ESP32

```c
#include "lcar.h"

LcarState state;
lcar_init(&state);

// Build ship
uint8_t harbor = lcar_add_room(&state, "Harbor", "Where vessels arrive");
uint8_t nav = lcar_add_room(&state, "Nav", "Navigation console");
lcar_connect(&state, harbor, "nav", nav);

// Wire real sensor
lcar_add_gauge(&state, nav, "heading", 0, 8000, 9000);

// In main loop: read UART, dispatch command
// On sensor timer: lcar_update_gauge(&state, nav, 0, compass_read());
```

## Simulation Bridge

Same binary protocol, different data source. Flip `OP_SIM_MODE` and the
gauge updates come from Isaac Sim / Gazebo instead of real sensors.
The agent doesn't know the difference.

## The Stack

```
Level 5: FLUX-LCAR Python server (full features, cloud)
Level 3: holodeck-c (C99, TCP, serial bridge)
Level 1: flux-lcar-esp32 (this repo — bare metal, 1.2KB)
Level 0: Physical controls (no code, just hands)
```

Each level is thinner. Each level fails less. Each level IS still a MUD.

## ESP32 / Arduino Support

### ESP-IDF
Copy `src/lcar.h` and `src/lcar_esp32_compat.h` into your ESP-IDF project.
The compatibility layer handles UART init, ADC reads, and FreeRTOS timers.

### Arduino IDE
1. Copy `src/lcar.h` and `src/lcar_esp32_compat.h` to your sketch
2. Open `examples/arduino/flux_lcar_bot/` in Arduino IDE
3. Select your ESP32 board
4. Upload — the MUD interpreter runs on real hardware

### Wiring (example)
```
GPIO36 (ADC1_CH0) → Compass sensor
GPIO39 (ADC1_CH3) → Speed sensor  
GPIO34 (ADC1_CH6) → Battery voltage
GPIO35 (ADC1_CH7) → Temperature sensor
UART0             → Serial monitor / command interface
```

### What Happens
The Arduino boots, creates 3 rooms (Harbor, Nav, Engine), wires 4 gauges to real ADC pins, and starts combat ticks at 1-second intervals. Open Serial Monitor at 115200 baud and type commands: LOOK, GO nav, STATUS, TICK.

Real sensor data flows through gauges. Alert thresholds trigger automatically. The MUD IS the robot's interface.
