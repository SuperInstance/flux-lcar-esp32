/* Standalone demo — compile with: gcc -DLCAR_STANDALONE -o lcar_esp32 src/lcar.h && ./lcar_esp32 */
#include "lcar.h"

/* For ESP32: just include lcar.h and call lcar_init/add_room/connect/add_gauge */
/* Wire UART receive to lcar_command() with the opcode from serial */
/* Wire gauge updates from ADC/I2C sensors to lcar_update_gauge() */
