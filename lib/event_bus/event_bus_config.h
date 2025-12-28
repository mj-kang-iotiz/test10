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

/* ==================== Event Bus Base Declarations (ESP-IDF style) ==================== */

/**
 * Event bus base strings (use pointer addresses for comparison)
 *
 * Similar to ESP-IDF's ESP_EVENT_DECLARE_BASE pattern.
 * These are string literals whose addresses are used as unique identifiers.
 */
extern const char EVENT_BUS_COMM[];      // Communication bus (GSM, LoRa)
extern const char EVENT_BUS_SENSOR[];    // Sensor bus (GPS)
extern const char EVENT_BUS_CONTROL[];   // Control bus (System)

/* ==================== Bus Configuration ==================== */

typedef struct {
    const char *name;        // Pointer to event bus base string
    uint32_t queue_depth;
    uint32_t priority;
} bus_config_t;

// Centralized configuration
typedef enum {
    BUS_ID_COMM = 0,
    BUS_ID_SENSOR,
    BUS_ID_CONTROL,
    BUS_ID_COUNT
} bus_id_t;

// Configuration array (internal use)
extern const bus_config_t g_bus_configs[BUS_ID_COUNT];

/* ==================== Efficient Access Functions ==================== */

/**
 * @brief Get event bus by base pointer (ESP-IDF style, O(1)!)
 *
 * Uses pointer address comparison only (no strcmp).
 * Auto-initializes on first access (lazy initialization).
 *
 * @param base Event bus base (EVENT_BUS_COMM, EVENT_BUS_SENSOR, etc.)
 * @return event_bus_t* Bus instance, NULL if not found
 *
 * Example:
 *   event_bus_t *bus = event_bus_get(EVENT_BUS_COMM);
 *   event_bus_publish(bus, EVT_GSM_CONNECTED, NULL, 0);
 */
event_bus_t* event_bus_get(const char *base);

/**
 * @brief Initialize all event buses explicitly
 *
 * Optional - can rely on lazy initialization instead.
 * Call this at startup if you want to control initialization timing.
 */
void event_bus_init_all(void);

/**
 * @brief Print statistics for a bus
 *
 * @param base Event bus base (EVENT_BUS_COMM, etc.)
 */
void event_bus_print_stats(const char *base);

/**
 * @brief Print statistics for all buses
 */
void event_bus_print_all_stats(void);

/* ==================== Convenience Macros ==================== */

/**
 * Ultra-short publish macros (most common use case)
 *
 * These automatically get the bus and publish in one call.
 * Perfect for use in callbacks and event publishing functions.
 */
#define PUBLISH_COMM(type, data, size) \
    event_bus_publish(event_bus_get(EVENT_BUS_COMM), type, data, size)

#define PUBLISH_SENSOR(type, data, size) \
    event_bus_publish(event_bus_get(EVENT_BUS_SENSOR), type, data, size)

#define PUBLISH_CONTROL(type, data, size) \
    event_bus_publish(event_bus_get(EVENT_BUS_CONTROL), type, data, size)

/**
 * Subscribe macros (for convenience)
 */
#define SUBSCRIBE_COMM(mask, handler) \
    event_bus_subscribe(event_bus_get(EVENT_BUS_COMM), mask, handler)

#define SUBSCRIBE_SENSOR(mask, handler) \
    event_bus_subscribe(event_bus_get(EVENT_BUS_SENSOR), mask, handler)

#define SUBSCRIBE_CONTROL(mask, handler) \
    event_bus_subscribe(event_bus_get(EVENT_BUS_CONTROL), mask, handler)

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
