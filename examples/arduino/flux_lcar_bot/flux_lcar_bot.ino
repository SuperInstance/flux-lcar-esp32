// FLUX-LCAR Arduino ESP32 Robot
// The quiet interpreter on real hardware

#include "lcar.h"
#include "lcar_esp32_compat.h"

LcarState state;
char resp[256];

void setup() {
    lcar_platform_init();
    lcar_init(&state);
    
    // Build the ship
    uint8_t harbor = lcar_add_room(&state, "Harbor", "Docking station");
    uint8_t nav = lcar_add_room(&state, "Nav", "Navigation");
    uint8_t eng = lcar_add_room(&state, "Engine", "Motor control");
    
    lcar_connect(&state, harbor, "nav", nav);
    lcar_connect(&state, nav, "harbor", harbor);
    lcar_connect(&state, nav, "eng", eng);
    lcar_connect(&state, eng, "nav", nav);
    
    // Wire gauges to real pins
    lcar_add_gauge(&state, nav, "compass", 0, 36000, 0);
    lcar_add_gauge(&state, nav, "speed", 0, 9000, 12000);
    lcar_add_gauge(&state, eng, "battery", 12500, 11000, 10000);
    lcar_add_gauge(&state, eng, "temp", 2500, 7000, 8500);
    
    // Start combat ticks at 1 second
    lcar_start_ticks(&state, NULL, 1000);
    
    Serial.println("FLUX-LCAR Robot Online");
    Serial.println("Commands: LOOK GO dir STATUS HELP");
}

void loop() {
    lcar_arduino_loop();
    
    // Read serial commands
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toUpperCase();
        
        if (cmd == "LOOK" || cmd == "L") {
            lcar_look(&state, resp, sizeof(resp));
            Serial.print(resp);
        } else if (cmd.startsWith("GO ")) {
            String dir = cmd.substring(3);
            dir.toLowerCase();
            lcar_command(&state, OP_GO, (uint8_t*)dir.c_str(), dir.length()+1, resp, sizeof(resp));
            Serial.print(resp);
        } else if (cmd == "STATUS") {
            lcar_command(&state, OP_STATUS, NULL, 0, resp, sizeof(resp));
            Serial.print(resp);
        } else if (cmd == "TICK") {
            lcar_command(&state, OP_TICK, NULL, 0, resp, sizeof(resp));
            Serial.print(resp);
        } else if (cmd == "HELP") {
            Serial.println("LOOK GO <dir> STATUS TICK HELP");
        }
    }
}
