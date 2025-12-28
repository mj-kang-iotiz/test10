/**
 * @file event_bus_config.h
 * @brief Event Bus Configuration and Access Layer
 *
 * Provides centralized configuration and efficient access to event buses.
 * Solves the global pointer + reusability problem.
 */

#ifndef EVENT_BUS_CONFIG_H
#define EVENT_BUS_CONFIG_H

#include "event_bus.h"

/* ==================== Bus IDs (for efficient access) ==================== */

typedef enum {
    BUS_COMM = 0,      // Communication bus (GSM, LoRa)
    BUS_SENSOR,        // Sensor bus (GPS)
    BUS_CONTROL,       // Control bus (System)
    BUS_COUNT          // Total number of buses
} bus_id_t;

/* ==================== Bus Configuration ==================== */

typedef struct {
    const char *name;
    uint32_t queue_depth;
    uint32_t priority;
} bus_config_t;

// Centralized configuration (can be modified easily)
static const bus_config_t BUS_CONFIGS[BUS_COUNT] = {
    [BUS_COMM]    = { "comm",    12, 4 },  // High priority
    [BUS_SENSOR]  = { "sensor",  20, 3 },  // Medium priority
    [BUS_CONTROL] = { "control",  5, 5 },  // Highest priority
};

/* ==================== Efficient Access Functions ==================== */

/**
 * @brief Get event bus by ID (O(1), very fast!)
 *
 * Auto-initializes on first access (lazy initialization).
 * Thread-safe.
 *
 * @param id Bus ID
 * @return event_bus_t* Bus instance
 */
event_bus_t* event_bus_get(bus_id_t id);

/**
 * @brief Get event bus by name (O(1) with string interning!)
 *
 * Only works with strings from BUS_CONFIGS (pointer comparison).
 * For runtime strings, use event_bus_get_instance() instead.
 *
 * @param name Bus name (must be from BUS_CONFIGS)
 * @return event_bus_t* Bus instance, NULL if not found
 */
event_bus_t* event_bus_get_by_name(const char *name);

/**
 * @brief Initialize all event buses
 *
 * Call this once at startup, or let event_bus_get() auto-initialize.
 */
void event_bus_init_all(void);

/**
 * @brief Get bus statistics and print
 */
void event_bus_print_stats(bus_id_t id);

/* ==================== Convenience Macros ==================== */

// Easy access macros (still O(1)!)
#define BUS_COMM_GET()    event_bus_get(BUS_COMM)
#define BUS_SENSOR_GET()  event_bus_get(BUS_SENSOR)
#define BUS_CONTROL_GET() event_bus_get(BUS_CONTROL)

// Ultra-short publish macros
#define PUBLISH_COMM(type, data, size)    event_bus_publish(BUS_COMM_GET(), type, data, size)
#define PUBLISH_SENSOR(type, data, size)  event_bus_publish(BUS_SENSOR_GET(), type, data, size)
#define PUBLISH_CONTROL(type, data, size) event_bus_publish(BUS_CONTROL_GET(), type, data, size)

/* ==================== Usage Examples ==================== */

#if 0
// Example 1: Simple usage
void gsm_on_connected(void) {
    PUBLISH_COMM(EVT_GSM_CONNECTED, NULL, 0);
}

// Example 2: With data
void gps_update(const gps_data_t *gps) {
    PUBLISH_SENSOR(EVT_GPS_UPDATE, gps, sizeof(gps_data_t));
}

// Example 3: Manual access
void some_function(void) {
    event_bus_t *bus = event_bus_get(BUS_COMM);
    event_bus_publish(bus, EVT_GSM_SEND, data, len);
}

// Example 4: Subscribe
void module_init(void) {
    event_bus_subscribe(BUS_COMM_GET(), 0, my_handler);
}
#endif

#endif /* EVENT_BUS_CONFIG_H */
