/**
 * @file event_bus_example.c
 * @brief Event Bus Example Implementation
 *
 * Demonstrates practical usage of domain-separated event buses.
 */

#include "event_bus_example.h"
#include <stdio.h>

/* ==================== Global Bus Pointers ==================== */

/**
 * Cached bus pointers for O(1) runtime access.
 * Initialized once at startup, then used directly.
 */
event_bus_t *g_comm_bus = NULL;
event_bus_t *g_sensor_bus = NULL;
event_bus_t *g_ctrl_bus = NULL;

/* ==================== Event Handlers ==================== */

/**
 * Handler for communication events (GSM, LoRa)
 */
static void on_comm_event(const event_msg_t *msg) {
    switch (msg->type) {
        case EVT_COMM_GSM_CONNECTED:
            // Handle GSM connected
            printf("[COMM] GSM connected\n");
            break;

        case EVT_COMM_GSM_DISCONNECTED:
            // Handle GSM disconnected
            printf("[COMM] GSM disconnected\n");
            break;

        case EVT_COMM_GSM_DATA_RECEIVED:
            // Handle received data
            printf("[COMM] GSM data received: %u bytes\n", (unsigned)msg->size);
            break;

        case EVT_COMM_LORA_RX_RECEIVED:
            // Handle LoRa reception
            printf("[COMM] LoRa RX received: %u bytes\n", (unsigned)msg->size);
            break;

        default:
            break;
    }
}

/**
 * Handler for sensor events (GPS)
 */
static void on_sensor_event(const event_msg_t *msg) {
    switch (msg->type) {
        case EVT_SENSOR_GPS_FIX_ACQUIRED:
            printf("[SENSOR] GPS fix acquired\n");
            break;

        case EVT_SENSOR_GPS_FIX_LOST:
            printf("[SENSOR] GPS fix lost\n");
            break;

        case EVT_SENSOR_GPS_DATA_UPDATE:
            if (msg->size == sizeof(gps_data_t)) {
                const gps_data_t *gps = (const gps_data_t*)msg->data;
                printf("[SENSOR] GPS: lat=%.6f, lon=%.6f, alt=%.2fm\n",
                       gps->latitude, gps->longitude, gps->altitude);
            }
            break;

        default:
            break;
    }
}

/**
 * Handler for control events (System)
 */
static void on_ctrl_event(const event_msg_t *msg) {
    switch (msg->type) {
        case EVT_CTRL_SHUTDOWN:
            printf("[CTRL] System shutdown requested\n");
            // Perform shutdown sequence
            break;

        case EVT_CTRL_REBOOT:
            printf("[CTRL] System reboot requested\n");
            // Perform reboot
            break;

        case EVT_CTRL_MODE_CHANGE:
            printf("[CTRL] Mode change requested\n");
            break;

        case EVT_CTRL_ERROR:
            printf("[CTRL] System error occurred\n");
            break;

        default:
            break;
    }
}

/* ==================== Initialization ==================== */

/**
 * @brief Initialize event bus system with domain separation
 *
 * Creates three buses:
 *   1. Communication bus (GSM, LoRa) - High priority
 *   2. Sensor bus (GPS) - Medium priority
 *   3. Control bus (System) - Highest priority
 */
void event_bus_system_init_full(void) {
    // Create communication bus
    g_comm_bus = event_bus_create("comm", 12, 4);
    if (!g_comm_bus) {
        printf("ERROR: Failed to create comm bus\n");
        return;
    }

    // Create sensor bus
    g_sensor_bus = event_bus_create("sensor", 20, 3);
    if (!g_sensor_bus) {
        printf("ERROR: Failed to create sensor bus\n");
        return;
    }

    // Create control bus
    g_ctrl_bus = event_bus_create("control", 5, 5);
    if (!g_ctrl_bus) {
        printf("ERROR: Failed to create control bus\n");
        return;
    }

    printf("Event bus system initialized successfully\n");
    printf("  - comm_bus:   queue=12, priority=4, pool=15\n");
    printf("  - sensor_bus: queue=20, priority=3, pool=15\n");
    printf("  - ctrl_bus:   queue=5,  priority=5, pool=15\n");
}

/**
 * @brief Subscribe all handlers to their respective buses
 */
void event_bus_subscribe_all(void) {
    // Subscribe to communication events
    event_bus_subscribe(g_comm_bus, 0, on_comm_event);

    // Subscribe to sensor events
    event_bus_subscribe(g_sensor_bus, 0, on_sensor_event);

    // Subscribe to control events
    event_bus_subscribe(g_ctrl_bus, 0, on_ctrl_event);

    printf("All event handlers subscribed\n");
}

/* ==================== Usage Examples ==================== */

/**
 * @brief Example: GSM module sends data
 */
void example_gsm_send_data(const uint8_t *data, size_t len) {
    // Publish GSM data sent event
    event_bus_publish(g_comm_bus, EVT_COMM_GSM_DATA_SENT, data, len);
}

/**
 * @brief Example: GSM connection state changed
 */
void example_gsm_connection_state(bool connected) {
    if (connected) {
        event_bus_publish(g_comm_bus, EVT_COMM_GSM_CONNECTED, NULL, 0);
    } else {
        event_bus_publish(g_comm_bus, EVT_COMM_GSM_DISCONNECTED, NULL, 0);
    }
}

/**
 * @brief Example: GPS position update
 */
void example_gps_position_update(float lat, float lon, float alt, uint8_t quality) {
    gps_data_t gps_data = {
        .latitude = lat,
        .longitude = lon,
        .altitude = alt,
        .fix_quality = quality
    };

    event_bus_publish(g_sensor_bus, EVT_SENSOR_GPS_DATA_UPDATE,
                     &gps_data, sizeof(gps_data_t));
}

/**
 * @brief Example: LoRa received packet
 */
void example_lora_rx(const uint8_t *packet, size_t len) {
    event_bus_publish(g_comm_bus, EVT_COMM_LORA_RX_RECEIVED, packet, len);
}

/**
 * @brief Example: System error occurred
 */
void example_system_error(uint32_t error_code) {
    event_bus_publish(g_ctrl_bus, EVT_CTRL_ERROR, &error_code, sizeof(error_code));
}

/* ==================== Statistics ==================== */

/**
 * @brief Print statistics for all buses
 */
void event_bus_print_all_stats(void) {
    uint32_t sub_count, pub_ok, pub_fail, pool_alloc, pool_peak, pool_fail;

    printf("\n===== Event Bus Statistics =====\n");

    // Communication bus stats
    event_bus_get_stats(g_comm_bus, &sub_count, &pub_ok, &pub_fail,
                       &pool_alloc, &pool_peak, &pool_fail);
    printf("\nCommunication Bus:\n");
    printf("  Subscribers:     %u\n", sub_count);
    printf("  Publish success: %u\n", pub_ok);
    printf("  Publish failed:  %u\n", pub_fail);
    printf("  Pool allocated:  %u / 15\n", pool_alloc);
    printf("  Pool peak:       %u\n", pool_peak);
    printf("  Pool failures:   %u\n", pool_fail);

    // Sensor bus stats
    event_bus_get_stats(g_sensor_bus, &sub_count, &pub_ok, &pub_fail,
                       &pool_alloc, &pool_peak, &pool_fail);
    printf("\nSensor Bus:\n");
    printf("  Subscribers:     %u\n", sub_count);
    printf("  Publish success: %u\n", pub_ok);
    printf("  Publish failed:  %u\n", pub_fail);
    printf("  Pool allocated:  %u / 15\n", pool_alloc);
    printf("  Pool peak:       %u\n", pool_peak);
    printf("  Pool failures:   %u\n", pool_fail);

    // Control bus stats
    event_bus_get_stats(g_ctrl_bus, &sub_count, &pub_ok, &pub_fail,
                       &pool_alloc, &pool_peak, &pool_fail);
    printf("\nControl Bus:\n");
    printf("  Subscribers:     %u\n", sub_count);
    printf("  Publish success: %u\n", pub_ok);
    printf("  Publish failed:  %u\n", pub_fail);
    printf("  Pool allocated:  %u / 15\n", pool_alloc);
    printf("  Pool peak:       %u\n", pool_peak);
    printf("  Pool failures:   %u\n", pool_fail);

    printf("\n================================\n");
}

/* ==================== Alternative: Using Registry ==================== */

/**
 * @brief Example of using registry instead of cached pointers
 *
 * This is LESS efficient (O(n) lookup + mutex overhead),
 * but more flexible if you need dynamic bus selection.
 */
void example_using_registry(void) {
    // Lookup by name (slower)
    event_bus_t *bus = event_bus_get_instance("comm");
    if (bus) {
        event_bus_publish(bus, EVT_COMM_GSM_CONNECTED, NULL, 0);
    }

    // This is equivalent to, but SLOWER than:
    event_bus_publish(g_comm_bus, EVT_COMM_GSM_CONNECTED, NULL, 0);

    // Recommendation: Use cached pointers for runtime operations!
}
