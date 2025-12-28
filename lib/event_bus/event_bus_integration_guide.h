/**
 * @file event_bus_integration_guide.h
 * @brief Event Bus Integration Guide for Existing Subsystems
 *
 * This guide shows how to integrate event bus with existing subsystems
 * that already use their own message queues (GSM, LoRa, GPS, etc.)
 *
 * Key points:
 * 1. Subsystems are PUBLISHERS only (not subscribers)
 * 2. Subsystems keep their existing queue/task structure
 * 3. Application layer subscribes to events
 * 4. Callbacks publish to event bus
 */

#ifndef EVENT_BUS_INTEGRATION_GUIDE_H
#define EVENT_BUS_INTEGRATION_GUIDE_H

#include "event_bus_config.h"

/* ==================== Event Type Definitions ==================== */

/**
 * Communication Events (GSM, LoRa)
 * Define in: modules/comm/comm_events.h
 */
typedef enum {
    EVT_COMM_GSM_CONNECTED = 0,
    EVT_COMM_GSM_DISCONNECTED,
    EVT_COMM_GSM_DATA_SENT,
    EVT_COMM_GSM_DATA_RECEIVED,
    EVT_COMM_GSM_ERROR,

    EVT_COMM_LORA_TX_DONE,
    EVT_COMM_LORA_RX_RECEIVED,
    EVT_COMM_LORA_ERROR,
} comm_event_type_t;

/**
 * Sensor Events (GPS)
 * Define in: modules/sensor/sensor_events.h
 */
typedef enum {
    EVT_SENSOR_GPS_FIX_ACQUIRED = 0,
    EVT_SENSOR_GPS_FIX_LOST,
    EVT_SENSOR_GPS_DATA_UPDATE,
    EVT_SENSOR_GPS_ERROR,
} sensor_event_type_t;

/* ==================== Data Structures ==================== */

typedef struct {
    float latitude;
    float longitude;
    float altitude;
    uint8_t satellites;
    uint8_t fix_quality;
} gps_position_t;

typedef struct {
    uint8_t signal_strength;
    uint32_t bytes_sent;
    uint32_t bytes_received;
} gsm_status_t;

/* ==================== File Structure ==================== */

/**
 * Recommended file organization:
 *
 * modules/gsm/
 *   ├── gsm_port.c          - Hardware abstraction (UART, AT commands)
 *   ├── gsm_task.c          - GSM task with own queue
 *   ├── gsm_events.c        - Event publishing logic ★
 *   └── gsm_events.h        - Event type definitions
 *
 * modules/gps/
 *   ├── gps_port.c          - Hardware abstraction
 *   ├── gps_task.c          - GPS task with own queue
 *   ├── gps_events.c        - Event publishing logic ★
 *   └── gps_events.h        - Event type definitions
 *
 * app/
 *   ├── app_main.c          - Application initialization
 *   ├── app_handlers.c      - Event bus subscribers ★
 *   └── app_handlers.h
 *
 * ★ = Where callbacks and handlers are defined
 */

/* ==================== Integration Pattern ==================== */

/**
 * STEP 1: In GSM module (Publisher)
 * File: modules/gsm/gsm_events.c
 */

// Called from GSM task when connection state changes
static inline void gsm_publish_connected(void) {
    event_bus_publish(BUS_COMM_GET(), EVT_COMM_GSM_CONNECTED, NULL, 0);
}

// Called from GSM UART callback when data arrives
static inline void gsm_publish_data_received(const uint8_t *data, size_t len) {
    // Publish directly from callback (non-blocking!)
    event_bus_publish(BUS_COMM_GET(), EVT_COMM_GSM_DATA_RECEIVED, data, len);
}

/**
 * STEP 2: In GPS module (Publisher)
 * File: modules/gps/gps_events.c
 */

// Called from GPS parser when position is updated
static inline void gps_publish_position(const gps_position_t *pos) {
    event_bus_publish(BUS_SENSOR_GET(), EVT_SENSOR_GPS_DATA_UPDATE,
                     pos, sizeof(gps_position_t));
}

/**
 * STEP 3: In Application (Subscriber)
 * File: app/app_handlers.c
 */

// This handler receives ALL communication events
void app_comm_handler(const event_msg_t *msg) {
    switch (msg->type) {
        case EVT_COMM_GSM_CONNECTED:
            // Handle GSM connection
            // app_on_gsm_connected();
            break;

        case EVT_COMM_GSM_DATA_RECEIVED:
            // Process received data
            // app_process_gsm_data(msg->data, msg->size);
            break;
    }
}

// This handler receives ALL sensor events
void app_sensor_handler(const event_msg_t *msg) {
    switch (msg->type) {
        case EVT_SENSOR_GPS_DATA_UPDATE:
            if (msg->size == sizeof(gps_position_t)) {
                const gps_position_t *pos = (const gps_position_t*)msg->data;
                // app_update_position(pos);
            }
            break;
    }
}

// Application initialization
static inline void app_handlers_init(void) {
    // Subscribe to communication events
    event_bus_subscribe(BUS_COMM_GET(), 0, app_comm_handler);

    // Subscribe to sensor events
    event_bus_subscribe(BUS_SENSOR_GET(), 0, app_sensor_handler);
}

/* ==================== Common Patterns ==================== */

/**
 * Pattern 1: Publish from existing callback
 *
 * You already have GSM callbacks, just add publish:
 */
void gsm_on_connect_callback(void) {  // Existing callback
    // Existing code
    gsm_set_connected_state(true);

    // Add event publish (non-blocking!)
    event_bus_publish(BUS_COMM_GET(), EVT_COMM_GSM_CONNECTED, NULL, 0);
}

/**
 * Pattern 2: Publish from task
 *
 * Your GSM task loop:
 */
void gsm_task(void *param) {
    while (1) {
        // Existing queue receive
        gsm_msg_t msg;
        if (xQueueReceive(gsm_queue, &msg, portMAX_DELAY) == pdTRUE) {
            // Process message
            gsm_process_message(&msg);

            // Publish to event bus if needed
            if (msg.type == GSM_MSG_DATA_RECEIVED) {
                event_bus_publish(BUS_COMM_GET(),
                                EVT_COMM_GSM_DATA_RECEIVED,
                                msg.data, msg.len);
            }
        }
    }
}

/**
 * Pattern 3: Multiple subscribers
 *
 * Different modules can subscribe to same event:
 */

// Logger module
void logger_comm_handler(const event_msg_t *msg) {
    // Log all communication events
    // log_write("COMM", msg->type, msg->data, msg->size);
}

// Statistics module
void stats_comm_handler(const event_msg_t *msg) {
    // Update statistics
    // stats_update_comm(msg->type);
}

// In their respective init functions:
// event_bus_subscribe(BUS_COMM_GET(), 0, logger_comm_handler);
// event_bus_subscribe(BUS_COMM_GET(), 0, stats_comm_handler);

/* ==================== Anti-Patterns (DON'T DO THIS) ==================== */

#if 0
// ❌ DON'T: Subscribe in GSM module
void gsm_init(void) {
    // GSM doesn't need to subscribe, only publish!
    // event_bus_subscribe(BUS_COMM_GET(), 0, gsm_handler);  // NO!
}

// ❌ DON'T: Block in callback
void gsm_uart_callback(uint8_t *data, size_t len) {
    // Don't do heavy processing here
    // process_data(data, len);  // NO! This blocks UART ISR

    // Just publish (non-blocking!)
    event_bus_publish(BUS_COMM_GET(), EVT_COMM_GSM_DATA_RECEIVED, data, len);
}

// ❌ DON'T: Publish from ISR without FromISR version
void UART_IRQHandler(void) {
    // event_bus_publish() uses xQueueSend, not safe in ISR!
    // Need to add event_bus_publish_from_isr() if needed
}
#endif

/* ==================== Best Practices ==================== */

/**
 * 1. Keep subsystems decoupled
 *    - GSM module doesn't know who subscribes
 *    - Subscribers don't know how GSM works
 *
 * 2. Publish early, process later
 *    - Publish from callbacks immediately (non-blocking)
 *    - Heavy processing happens in subscriber handlers
 *
 * 3. Use clear event names
 *    - EVT_COMM_GSM_CONNECTED (good)
 *    - EVT_GSM_1 (bad)
 *
 * 4. Keep event data small
 *    - Max 256 bytes
 *    - For large data, publish pointer/handle
 *
 * 5. One module = One file for events
 *    - gsm_events.c for all GSM event publishing
 *    - Easy to find and maintain
 */

#endif /* EVENT_BUS_INTEGRATION_GUIDE_H */
