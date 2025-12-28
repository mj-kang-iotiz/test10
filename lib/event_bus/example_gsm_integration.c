/**
 * @file example_gsm_integration.c
 * @brief Example: How to integrate event bus with existing GSM module
 *
 * This shows a realistic GSM module that:
 * - Already has its own task and queue
 * - Has UART callbacks
 * - Publishes events to event bus
 */

#include "event_bus_config.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>

/* ==================== GSM Module (Existing Code) ==================== */

// GSM module's own queue (already exists in your code)
static QueueHandle_t gsm_queue = NULL;
static TaskHandle_t gsm_task_handle = NULL;

// GSM internal message types
typedef enum {
    GSM_MSG_SEND_AT,
    GSM_MSG_DATA_RECEIVED,
    GSM_MSG_CONNECT,
    GSM_MSG_DISCONNECT,
} gsm_msg_type_t;

typedef struct {
    gsm_msg_type_t type;
    uint8_t data[256];
    size_t len;
} gsm_msg_t;

// GSM state
typedef struct {
    bool connected;
    uint8_t signal_strength;
} gsm_state_t;

static gsm_state_t g_gsm_state = {0};

/* ==================== Event Definitions ==================== */

// Communication events (from event_bus_integration_guide.h)
typedef enum {
    EVT_COMM_GSM_CONNECTED = 0,
    EVT_COMM_GSM_DISCONNECTED,
    EVT_COMM_GSM_DATA_SENT,
    EVT_COMM_GSM_DATA_RECEIVED,
    EVT_COMM_GSM_ERROR,
} comm_event_type_t;

/* ==================== Event Publishing Functions ==================== */

/**
 * These functions are called from various places in GSM module
 * to publish events to the event bus.
 *
 * File organization:
 *   - Put these in: modules/gsm/gsm_events.c
 *   - Declare in:   modules/gsm/gsm_events.h
 */

static void gsm_publish_connected(void) {
    event_bus_publish(BUS_COMM_GET(), EVT_COMM_GSM_CONNECTED, NULL, 0);
}

static void gsm_publish_disconnected(void) {
    event_bus_publish(BUS_COMM_GET(), EVT_COMM_GSM_DISCONNECTED, NULL, 0);
}

static void gsm_publish_data_received(const uint8_t *data, size_t len) {
    event_bus_publish(BUS_COMM_GET(), EVT_COMM_GSM_DATA_RECEIVED, data, len);
}

static void gsm_publish_data_sent(size_t len) {
    event_bus_publish(BUS_COMM_GET(), EVT_COMM_GSM_DATA_SENT, &len, sizeof(len));
}

/* ==================== GSM Task (Your Existing Code) ==================== */

/**
 * Your existing GSM task - just add event publishing!
 */
static void gsm_task(void *param) {
    gsm_msg_t msg;

    while (1) {
        // Your existing queue receive
        if (xQueueReceive(gsm_queue, &msg, portMAX_DELAY) == pdTRUE) {

            switch (msg.type) {
                case GSM_MSG_CONNECT:
                    // Existing connection logic
                    g_gsm_state.connected = true;

                    // ★ ADD: Publish event to bus
                    gsm_publish_connected();
                    break;

                case GSM_MSG_DISCONNECT:
                    // Existing disconnection logic
                    g_gsm_state.connected = false;

                    // ★ ADD: Publish event to bus
                    gsm_publish_disconnected();
                    break;

                case GSM_MSG_DATA_RECEIVED:
                    // Process received data
                    // ... your existing code ...

                    // ★ ADD: Publish to event bus
                    gsm_publish_data_received(msg.data, msg.len);
                    break;

                case GSM_MSG_SEND_AT:
                    // Send AT command
                    // ... your existing code ...

                    // ★ ADD: Publish confirmation
                    gsm_publish_data_sent(msg.len);
                    break;
            }
        }
    }
}

/* ==================== UART Callbacks (Your Existing Code) ==================== */

/**
 * Your existing UART callback - just add event publishing!
 *
 * WARNING: If this is called from ISR, you need FromISR version!
 */
void gsm_uart_rx_callback(uint8_t *data, size_t len) {
    // Option 1: Forward to GSM queue (existing approach)
    gsm_msg_t msg = {
        .type = GSM_MSG_DATA_RECEIVED,
        .len = len
    };
    memcpy(msg.data, data, len);
    xQueueSend(gsm_queue, &msg, 0);

    // Option 2: Also publish directly to event bus (faster!)
    // Note: Only if not in ISR context!
    if (!xPortIsInsideInterrupt()) {
        gsm_publish_data_received(data, len);
    }
}

/**
 * Connection callback (e.g., from AT command response parser)
 */
void gsm_on_connection_established(void) {
    // Update state
    g_gsm_state.connected = true;

    // ★ Publish event immediately (non-blocking!)
    gsm_publish_connected();
}

/* ==================== Public GSM API ==================== */

/**
 * Initialize GSM module
 */
void gsm_init(void) {
    // Create GSM's own queue
    gsm_queue = xQueueCreate(10, sizeof(gsm_msg_t));

    // Create GSM task
    xTaskCreate(gsm_task, "gsm", 1024, NULL, 3, &gsm_task_handle);

    // Initialize hardware
    // gsm_hw_init();

    // NOTE: We DON'T subscribe to event bus here!
    // GSM is a PUBLISHER only.
}

/**
 * Send data via GSM
 */
void gsm_send_data(const uint8_t *data, size_t len) {
    // Your existing send logic
    // ...

    // After successful send, publish event
    gsm_publish_data_sent(len);
}

/* ==================== Application Handler ==================== */

/**
 * Application subscribes to GSM events
 *
 * File location: app/app_handlers.c
 */
void app_comm_handler(const event_msg_t *msg) {
    switch (msg->type) {
        case EVT_COMM_GSM_CONNECTED:
            // Application handles connection
            // printf("App: GSM connected, starting data sync...\n");
            // app_start_data_sync();
            break;

        case EVT_COMM_GSM_DISCONNECTED:
            // printf("App: GSM disconnected, retrying...\n");
            // app_schedule_reconnect();
            break;

        case EVT_COMM_GSM_DATA_RECEIVED:
            // printf("App: Received %u bytes from GSM\n", msg->size);
            // app_process_server_data(msg->data, msg->size);
            break;

        case EVT_COMM_GSM_DATA_SENT:
            // printf("App: Data sent successfully\n");
            // app_on_send_complete();
            break;
    }
}

/* ==================== Logger Handler ==================== */

/**
 * Logger module also subscribes (different handler!)
 *
 * File location: modules/logger/logger_handlers.c
 */
void logger_comm_handler(const event_msg_t *msg) {
    const char *event_name = "UNKNOWN";

    switch (msg->type) {
        case EVT_COMM_GSM_CONNECTED:    event_name = "GSM_CONNECTED"; break;
        case EVT_COMM_GSM_DISCONNECTED: event_name = "GSM_DISCONNECTED"; break;
        case EVT_COMM_GSM_DATA_RECEIVED: event_name = "GSM_DATA_RX"; break;
        case EVT_COMM_GSM_DATA_SENT:    event_name = "GSM_DATA_TX"; break;
    }

    // Log to file, UART, SD card, etc.
    // log_write(msg->timestamp, "COMM", event_name, msg->data, msg->size);
}

/* ==================== Main Initialization ==================== */

/**
 * System initialization
 *
 * File location: main.c or app/app_main.c
 */
void system_init(void) {
    // 1. Initialize event bus (lazy init also works)
    event_bus_init_all();

    // 2. Subscribe application handlers
    event_bus_subscribe(BUS_COMM_GET(), 0, app_comm_handler);

    // 3. Subscribe logger handlers
    event_bus_subscribe(BUS_COMM_GET(), 0, logger_comm_handler);

    // 4. Initialize subsystems (GSM, GPS, etc.)
    gsm_init();  // GSM doesn't subscribe, only publishes!
    // gps_init();
    // lora_init();
}

/* ==================== Summary ==================== */

/**
 * File Organization:
 *
 * modules/gsm/
 *   ├── gsm.h                  - Public API
 *   ├── gsm.c                  - GSM task and core logic
 *   ├── gsm_port.c             - Hardware (UART, AT commands)
 *   ├── gsm_events.h           - Event type definitions
 *   └── gsm_events.c           - Event publishing functions ★
 *                                (gsm_publish_connected, etc.)
 *
 * app/
 *   ├── app_main.c             - System initialization
 *   ├── app_handlers.h         - Handler declarations
 *   └── app_handlers.c         - Event subscribers ★
 *                                (app_comm_handler, etc.)
 *
 * modules/logger/
 *   └── logger_handlers.c      - Logger event subscribers ★
 *
 *
 * Key Points:
 *
 * 1. ★ gsm_events.c
 *    - Contains: gsm_publish_xxx() functions
 *    - Called from: gsm task, callbacks
 *    - Purpose: Publishing events
 *
 * 2. ★ app_handlers.c
 *    - Contains: app_comm_handler() function
 *    - Subscribed in: app_main.c
 *    - Purpose: Receiving and processing events
 *
 * 3. Separation of concerns:
 *    - GSM module: Publishes events, doesn't know subscribers
 *    - App: Subscribes to events, doesn't know GSM internals
 *    - Logger: Also subscribes, completely independent
 */
