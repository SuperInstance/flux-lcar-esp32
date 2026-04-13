/**
 * flux-lcar-esp32 — Bare Metal MUD Interpreter
 * 
 * The quiet interpreter. Fits in 4KB RAM. Runs on anything.
 * Same rooms, gauges, commands as the full server.
 * No dynamic allocation. Fixed buffers. Binary commands.
 * 
 * This is Level 1 — between the full C server and physical controls.
 * Works on ESP32, STM32, anything with a UART and 4KB RAM.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

// ═══════════════════════════════════════════
// Configuration — tune for target hardware
// ═══════════════════════════════════════════

#define LCAR_MAX_ROOMS       16
#define LCAR_MAX_EXITS       4
#define LCAR_MAX_GAUGES      4
#define LCAR_MAX_AGENTS      4
#define LCAR_MAX_NAME        16
#define LCAR_MAX_LINE        64
#define LCAR_CMD_BUF         32

// ═══════════════════════════════════════════
// Opcodes — 1-byte commands, no parsing needed
// ═══════════════════════════════════════════

#define OP_LOOK      0x01
#define OP_GO        0x02
#define OP_SAY       0x03
#define OP_TELL      0x04
#define OP_WHO       0x05
#define OP_HELP      0x06
#define OP_QUIT      0x07
#define OP_NOTE_READ 0x08
#define OP_NOTE_WRITE 0x09
#define OP_GAUGE     0x0A
#define OP_ALERT     0x0F
#define OP_STATUS    0x10
#define OP_BOOT      0x11
#define OP_SHUTDOWN  0x12
#define OP_TICK      0x13
#define OP_SIM_MODE  0x20  // switch to simulation data source
#define OP_REAL_MODE 0x21  // switch to real sensor data source
#define OP_ECHO      0xFF

// ═══════════════════════════════════════════
// Alert levels
// ═══════════════════════════════════════════

#define ALERT_GREEN  0
#define ALERT_YELLOW 1
#define ALERT_RED    2

// ═══════════════════════════════════════════
// Data structures — no malloc, all static
// ═══════════════════════════════════════════

typedef struct {
    char     name[LCAR_MAX_NAME];
    int16_t  value;       // fixed-point: value/100.0
    uint16_t yellow_thresh;
    uint16_t red_thresh;
} LcarGauge;

typedef struct {
    char     name[LCAR_MAX_NAME];
    uint8_t  target_room;
} LcarExit;

typedef struct {
    char      name[LCAR_MAX_NAME];
    char      desc[LCAR_MAX_LINE];
    LcarExit  exits[LCAR_MAX_EXITS];
    LcarGauge gauges[LCAR_MAX_GAUGES];
    uint8_t   exit_count;
    uint8_t   gauge_count;
    uint8_t   booted;
    uint8_t   agent_count;
} LcarRoom;

typedef struct {
    char     name[LCAR_MAX_NAME];
    uint8_t  room;
    uint8_t  permission;
} LcarAgent;

typedef struct {
    LcarRoom   rooms[LCAR_MAX_ROOMS];
    LcarAgent  agents[LCAR_MAX_AGENTS];
    uint8_t    room_count;
    uint8_t    agent_count;
    uint8_t    current_room;     // which room the connected agent is in
    uint8_t    alert_level;
    uint8_t    tick_count;
    uint8_t    sim_mode;         // 0=real sensors, 1=simulation
} LcarState;

// ═══════════════════════════════════════════
// API — the full interpreter in ~200 lines
// ═══════════════════════════════════════════

// Initialize state
void lcar_init(LcarState *s) {
    memset(s, 0, sizeof(LcarState));
    s->current_room = 0;
    s->alert_level = ALERT_GREEN;
    s->sim_mode = 0;
}

// Add a room, returns room index
uint8_t lcar_add_room(LcarState *s, const char *name, const char *desc) {
    if (s->room_count >= LCAR_MAX_ROOMS) return 0xFF;
    uint8_t idx = s->room_count++;
    strncpy(s->rooms[idx].name, name, LCAR_MAX_NAME - 1);
    strncpy(s->rooms[idx].desc, desc, LCAR_MAX_LINE - 1);
    return idx;
}

// Connect rooms
void lcar_connect(LcarState *s, uint8_t from, const char *dir, uint8_t to) {
    if (from >= s->room_count || to >= s->room_count) return;
    LcarRoom *r = &s->rooms[from];
    if (r->exit_count >= LCAR_MAX_EXITS) return;
    LcarExit *e = &r->exits[r->exit_count++];
    strncpy(e->name, dir, LCAR_MAX_NAME - 1);
    e->target_room = to;
}

// Add gauge to room
void lcar_add_gauge(LcarState *s, uint8_t room, const char *name,
                     int16_t value, uint16_t yellow, uint16_t red) {
    if (room >= s->room_count) return;
    LcarRoom *r = &s->rooms[room];
    if (r->gauge_count >= LCAR_MAX_GAUGES) return;
    LcarGauge *g = &r->gauges[r->gauge_count++];
    strncpy(g->name, name, LCAR_MAX_NAME - 1);
    g->value = value;
    g->yellow_thresh = yellow;
    g->red_thresh = red;
}

// Update gauge value (from real sensor or simulation)
void lcar_update_gauge(LcarState *s, uint8_t room, uint8_t gauge_idx, int16_t value) {
    if (room >= s->room_count) return;
    LcarRoom *r = &s->rooms[room];
    if (gauge_idx >= r->gauge_count) return;
    r->gauges[gauge_idx].value = value;
    
    // Check thresholds and auto-escalate
    if (value >= r->gauges[gauge_idx].red_thresh) {
        s->alert_level = ALERT_RED;
    } else if (value >= r->gauges[gauge_idx].yellow_thresh && s->alert_level == ALERT_GREEN) {
        s->alert_level = ALERT_YELLOW;
    }
}

// Look at current room
void lcar_look(LcarState *s, char *buf, uint16_t bufsize) {
    LcarRoom *r = &s->rooms[s->current_room];
    int len = 0;
    
    len += snprintf(buf + len, bufsize - len, "%s\n%s\n", r->name, r->desc);
    
    // Gauges as simple bars
    for (int i = 0; i < r->gauge_count; i++) {
        LcarGauge *g = &r->gauges[i];
        char status = (g->value >= g->red_thresh) ? '!' : 
                      (g->value >= g->yellow_thresh) ? '~' : '.';
        len += snprintf(buf + len, bufsize - len, 
                       "  %s: %d.%02d [%c]\n", 
                       g->name, g->value / 100, g->value % 100, status);
    }
    
    // Exits
    if (r->exit_count > 0) {
        len += snprintf(buf + len, bufsize - len, "Exits: ");
        for (int i = 0; i < r->exit_count; i++) {
            len += snprintf(buf + len, bufsize - len, "%s ", r->exits[i].name);
        }
        len += snprintf(buf + len, bufsize - len, "\n");
    }
    
    // Alert level
    if (s->alert_level > ALERT_GREEN) {
        len += snprintf(buf + len, bufsize - len, 
                       s->alert_level == ALERT_RED ? "ALERT: RED\n" : "ALERT: YELLOW\n");
    }
    
    // Mode indicator
    len += snprintf(buf + len, bufsize - len, 
                   s->sim_mode ? "[SIM]\n" : "[REAL]\n");
}

// Process one binary command, returns response length
uint16_t lcar_command(LcarState *s, uint8_t opcode, 
                      const uint8_t *args, uint8_t arg_len,
                      char *resp, uint16_t resp_size) {
    switch (opcode) {
        case OP_LOOK:
            lcar_look(s, resp, resp_size);
            return strlen(resp);
            
        case OP_GO: {
            // args = exit name (null-terminated string)
            LcarRoom *r = &s->rooms[s->current_room];
            for (int i = 0; i < r->exit_count; i++) {
                if (strncmp(r->exits[i].name, (const char*)args, LCAR_MAX_NAME) == 0) {
                    s->current_room = r->exits[i].target_room;
                    r->agent_count--;
                    s->rooms[s->current_room].agent_count++;
                    s->rooms[s->current_room].booted = 1;
                    lcar_look(s, resp, resp_size);
                    return strlen(resp);
                }
            }
            snprintf(resp, resp_size, "No exit '%s'\n", (const char*)args);
            return strlen(resp);
        }
        
        case OP_SAY: {
            snprintf(resp, resp_size, "You say: %s\n", (const char*)args);
            return strlen(resp);
        }
        
        case OP_STATUS: {
            int len = snprintf(resp, resp_size,
                "Rooms:%d Agents:%d Alert:%d Tick:%d Mode:%s\n",
                s->room_count, s->agent_count, s->alert_level,
                s->tick_count, s->sim_mode ? "SIM" : "REAL");
            return len;
        }
        
        case OP_GAUGE: {
            // args: [room_idx, gauge_idx, value_hi, value_lo]
            if (arg_len >= 4) {
                uint8_t room = args[0];
                uint8_t gauge = args[1];
                int16_t val = (args[2] << 8) | args[3];
                lcar_update_gauge(s, room, gauge, val);
                snprintf(resp, resp_size, "Gauge updated\n");
                return strlen(resp);
            }
            snprintf(resp, resp_size, "Bad gauge packet\n");
            return strlen(resp);
        }
        
        case OP_TICK: {
            s->tick_count++;
            // Check all gauges in current room for alerts
            LcarRoom *r = &s->rooms[s->current_room];
            int alerts = 0;
            for (int i = 0; i < r->gauge_count; i++) {
                if (r->gauges[i].value >= r->gauges[i].red_thresh) alerts++;
            }
            snprintf(resp, resp_size, "Tick %d. Alerts: %d\n", s->tick_count, alerts);
            return strlen(resp);
        }
        
        case OP_SIM_MODE:
            s->sim_mode = 1;
            snprintf(resp, resp_size, "Switched to SIMULATION\n");
            return strlen(resp);
            
        case OP_REAL_MODE:
            s->sim_mode = 0;
            snprintf(resp, resp_size, "Switched to REAL sensors\n");
            return strlen(resp);
            
        case OP_ALERT: {
            if (arg_len >= 1) {
                s->alert_level = args[0];
                snprintf(resp, resp_size, "Alert set to %d\n", s->alert_level);
            } else {
                snprintf(resp, resp_size, "Alert: %d\n", s->alert_level);
            }
            return strlen(resp);
        }
        
        case OP_HELP:
            snprintf(resp, resp_size,
                "LOOK GO dir SAY msg STATUS GAUGE r g vh vl\n"
                "TICK SIM REAL ALERT level HELP QUIT\n");
            return strlen(resp);
            
        case OP_QUIT:
            snprintf(resp, resp_size, "Fair winds.\n");
            return strlen(resp);
            
        default:
            snprintf(resp, resp_size, "Unknown: 0x%02X\n", opcode);
            return strlen(resp);
    }
}

// ═══════════════════════════════════════════
// Demo — runs on any platform with printf
// ═══════════════════════════════════════════

#ifdef LCAR_STANDALONE
#include <stdio.h>

int main(void) {
    printf("=== FLUX-LCAR ESP32 Bare Metal Interpreter ===\n\n");
    
    // Initialize
    LcarState state;
    lcar_init(&state);
    
    // Build the ship (same rooms as full server)
    uint8_t harbor = lcar_add_room(&state, "Harbor", "Where vessels arrive");
    uint8_t nav    = lcar_add_room(&state, "Navigation", "Compass, heading, rudder");
    uint8_t eng    = lcar_add_room(&state, "Engineering", "Gauges fighting");
    
    lcar_connect(&state, harbor, "nav", nav);
    lcar_connect(&state, nav, "harbor", harbor);
    lcar_connect(&state, nav, "eng", eng);
    lcar_connect(&state, eng, "nav", nav);
    
    // Wire gauges (values in hundredths: 24700 = 247.00)
    lcar_add_gauge(&state, nav, "heading", 24700, 8000, 9000);
    lcar_add_gauge(&state, nav, "commanded", 25000, 0, 0);
    lcar_add_gauge(&state, nav, "rudder", -200, 7000, 9000);
    lcar_add_gauge(&state, eng, "cpu", 4500, 7000, 9000);
    lcar_add_gauge(&state, eng, "error_rate", 210, 5000, 8000);
    
    char buf[256];
    
    // Look around
    printf("--- Look (harbor) ---\n");
    lcar_look(&state, buf, sizeof(buf));
    printf("%s", buf);
    
    // Go to nav
    printf("--- Go nav ---\n");
    lcar_command(&state, OP_GO, (uint8_t*)"nav", 4, buf, sizeof(buf));
    printf("%s", buf);
    
    // Tick with normal gauges
    printf("--- Tick (green) ---\n");
    lcar_command(&state, OP_TICK, NULL, 0, buf, sizeof(buf));
    printf("%s", buf);
    
    // Update error_rate to trigger yellow (simulated sensor reading)
    printf("--- Gauge update: error_rate spikes ---\n");
    uint8_t gauge_args[] = {1, 1, 0x19, 0x00}; // room 1, gauge 1, value 6400
    lcar_command(&state, OP_GAUGE, gauge_args, 4, buf, sizeof(buf));
    printf("%s", buf);
    
    // Tick again — should show alert
    printf("--- Tick (yellow alert) ---\n");
    lcar_command(&state, OP_TICK, NULL, 0, buf, sizeof(buf));
    printf("%s", buf);
    
    // Switch to sim mode
    printf("--- Switch to simulation ---\n");
    lcar_command(&state, OP_SIM_MODE, NULL, 0, buf, sizeof(buf));
    printf("%s", buf);
    
    // Look again — should show [SIM]
    lcar_look(&state, buf, sizeof(buf));
    printf("%s", buf);
    
    // Status
    printf("--- Status ---\n");
    lcar_command(&state, OP_STATUS, NULL, 0, buf, sizeof(buf));
    printf("%s", buf);
    
    // Memory footprint
    printf("\n--- Footprint ---\n");
    printf("LcarState: %zu bytes\n", sizeof(LcarState));
    printf("LcarRoom:  %zu bytes each, %d max\n", sizeof(LcarRoom), LCAR_MAX_ROOMS);
    printf("Total:     %zu bytes static, zero malloc\n", sizeof(LcarState));
    
    return 0;
}
#endif
