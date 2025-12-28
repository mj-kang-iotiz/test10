/**
 * @file event_bus_example.h
 * @brief Event Bus Example - Domain-separated bus configuration
 *
 * This file demonstrates how to set up multiple event buses for different
 * subsystems (GSM, LoRa, GPS, etc.) with efficient pointer caching.
 */

#ifndef EVENT_BUS_EXAMPLE_H
#define EVENT_BUS_EXAMPLE_H

#include "event_bus.h"

/* ==================== Event Type Definitions ==================== */

/* Communication Bus Events (GSM, LoRa) */
enum {
    EVT_COMM_GSM_CONNECTED = 0,
    EVT_COMM_GSM_DISCONNECTED,
    EVT_COMM_GSM_DATA_SENT,
    EVT_COMM_GSM_DATA_RECEIVED,
    EVT_COMM_LORA_TX_COMPLETE,
    EVT_COMM_LORA_RX_RECEIVED,
    EVT_COMM_LORA_ERROR,
};

/* Sensor Bus Events (GPS) */
enum {
    EVT_SENSOR_GPS_FIX_ACQUIRED = 0,
    EVT_SENSOR_GPS_FIX_LOST,
    EVT_SENSOR_GPS_DATA_UPDATE,
    EVT_SENSOR_GPS_NMEA_RECEIVED,
    EVT_SENSOR_GPS_UBX_RECEIVED,
};

/* Control Bus Events (System commands) */
enum {
    EVT_CTRL_SHUTDOWN = 0,
    EVT_CTRL_REBOOT,
    EVT_CTRL_MODE_CHANGE,
    EVT_CTRL_CONFIG_UPDATE,
    EVT_CTRL_ERROR,
};

/* ==================== Global Bus Pointers (Cached) ==================== */

/**
 * Cached bus pointers for O(1) access.
 * Initialize once at startup, then use directly without registry lookup.
 */
extern event_bus_t *g_comm_bus;     // Communication bus (GSM, LoRa)
extern event_bus_t *g_sensor_bus;   // Sensor bus (GPS)
extern event_bus_t *g_ctrl_bus;     // Control bus (System)

/* ==================== Initialization ==================== */

/**
 * @brief Initialize all event buses for the system
 *
 * Call this once during system startup.
 * Bus configuration:
 *   - comm_bus:   queue=12, priority=4 (high)
 *   - sensor_bus: queue=20, priority=3 (medium)
 *   - ctrl_bus:   queue=5,  priority=5 (highest)
 */
static inline void event_bus_system_init(void) {
    // Communication bus (GSM, LoRa) - High priority
    g_comm_bus = event_bus_create("comm", 12, 4);

    // Sensor bus (GPS) - Medium priority, larger queue
    g_sensor_bus = event_bus_create("sensor", 20, 3);

    // Control bus (System commands) - Highest priority, small queue
    g_ctrl_bus = event_bus_create("control", 5, 5);
}

/* ==================== Usage Examples ==================== */

/**
 * Example 1: GSM subsystem publishing an event
 */
static inline void gsm_example_publish_connected(void) {
    // Simple event with no data
    event_bus_publish(g_comm_bus, EVT_COMM_GSM_CONNECTED, NULL, 0);
}

/**
 * Example 2: GPS subsystem publishing data
 */
typedef struct {
    float latitude;
    float longitude;
    float altitude;
    uint8_t fix_quality;
} gps_data_t;

static inline void gps_example_publish_data(const gps_data_t *gps_data) {
    // Publish with data payload
    event_bus_publish(g_sensor_bus, EVT_SENSOR_GPS_DATA_UPDATE,
                     gps_data, sizeof(gps_data_t));
}

/**
 * Example 3: Event handler for GPS updates
 */
static void on_gps_data_update(const event_msg_t *msg) {
    if (msg->type == EVT_SENSOR_GPS_DATA_UPDATE && msg->size == sizeof(gps_data_t)) {
        const gps_data_t *gps = (const gps_data_t*)msg->data;

        // Process GPS data
        // printf("GPS: lat=%.6f, lon=%.6f, alt=%.2f\n",
        //        gps->latitude, gps->longitude, gps->altitude);
    }
}

/**
 * Example 4: Subscribe to events
 */
static inline void gps_example_subscribe(void) {
    // Subscribe to all GPS events (event_mask = 0 means "all")
    event_bus_subscribe(g_sensor_bus, 0, on_gps_data_update);

    // Or subscribe to specific event types using bitmask:
    // uint32_t mask = (1 << EVT_SENSOR_GPS_DATA_UPDATE) |
    //                 (1 << EVT_SENSOR_GPS_FIX_ACQUIRED);
    // event_bus_subscribe(g_sensor_bus, mask, on_gps_data_update);
}

/**
 * Example 5: Get bus statistics
 */
static inline void event_bus_example_print_stats(void) {
    uint32_t sub_count, pub_ok, pub_fail, pool_alloc, pool_peak, pool_fail;

    // Get stats for communication bus
    event_bus_get_stats(g_comm_bus, &sub_count, &pub_ok, &pub_fail,
                       &pool_alloc, &pool_peak, &pool_fail);

    // Print or log statistics
    // printf("Comm Bus: subscribers=%u, pub_ok=%u, pub_fail=%u\n",
    //        sub_count, pub_ok, pub_fail);
    // printf("  Pool: allocated=%u, peak=%u, failures=%u\n",
    //        pool_alloc, pool_peak, pool_fail);
}

/* ==================== Architecture Notes ==================== */

/**
 * WHY DOMAIN SEPARATION?
 *
 * 1. Independent Priorities
 *    - Control events (shutdown, error) are highest priority
 *    - Communication events are high priority (GSM, LoRa)
 *    - Sensor events are medium priority (GPS data)
 *
 * 2. No Cross-Interference
 *    - GPS flood won't block GSM events
 *    - Each bus has its own message pool (15 messages each)
 *
 * 3. Predictable Memory
 *    - comm_bus:   15 msgs * 256 bytes = 3.75 KB
 *    - sensor_bus: 15 msgs * 256 bytes = 3.75 KB
 *    - ctrl_bus:   15 msgs * 256 bytes = 3.75 KB
 *    - Total pool memory: ~11.25 KB
 *
 * 4. Easy Debugging
 *    - Each subsystem has clear event boundaries
 *    - Statistics per bus
 */

/**
 * WHY POINTER CACHING?
 *
 * Registry lookup (string-based):
 *   - O(n) time complexity
 *   - Mutex lock/unlock overhead
 *   - Good for flexibility and initialization
 *
 * Cached pointers:
 *   - O(1) direct access
 *   - No mutex overhead
 *   - Perfect for runtime event publishing
 *
 * Best practice:
 *   - Use registry at initialization: event_bus_create()
 *   - Cache pointer in global variable: g_comm_bus
 *   - Use cached pointer at runtime: event_bus_publish(g_comm_bus, ...)
 */

#endif /* EVENT_BUS_EXAMPLE_H */
