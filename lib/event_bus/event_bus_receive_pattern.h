/**
 * @file event_bus_receive_pattern.h
 * @brief Event Bus Receive Patterns - Callback vs Direct Receive
 *
 * Compares three approaches:
 * 1. Pure callback (current implementation)
 * 2. Direct receive (alternative)
 * 3. Hybrid (recommended for heavy processing)
 */

#ifndef EVENT_BUS_RECEIVE_PATTERN_H
#define EVENT_BUS_RECEIVE_PATTERN_H

#include "event_bus_config.h"

/* ==================== Pattern 1: Pure Callback (Current) ==================== */

/**
 * Use case: Lightweight processing, multiple subscribers
 * Example: Logging, statistics, simple notifications
 */

// Logger module
void logger_handler(const event_msg_t *msg) {
    // Fast, non-blocking
    log_write(msg->type, msg->data, msg->size);
}

// Statistics module
void stats_handler(const event_msg_t *msg) {
    // Fast, non-blocking
    stats_increment(msg->type);
}

// LED indicator
void led_handler(const event_msg_t *msg) {
    // Fast, non-blocking
    if (msg->type == EVT_GSM_CONNECTED) {
        led_set(LED_GREEN, true);
    }
}

// All subscribe to same bus
static inline void pattern1_init(void) {
    SUBSCRIBE_COMM(0, logger_handler);   // All events
    SUBSCRIBE_COMM(0, stats_handler);    // All events
    SUBSCRIBE_COMM(0, led_handler);      // All events
}

/**
 * Pros:
 * - Multiple subscribers to same event (1:N broadcast)
 * - Simple, no extra queues
 * - Automatic dispatch
 *
 * Cons:
 * - Runs in event bus task context
 * - Cannot block (no delay, no queue receive)
 * - Hard to debug (complex stack trace)
 */

/* ==================== Pattern 2: Direct Receive (Alternative) ==================== */

/**
 * Use case: Heavy processing, blocking operations
 * Example: File I/O, network operations, display updates
 *
 * Note: This requires modifying event_bus to support direct receive API
 */

#if 0  // Not implemented in current event_bus, shown for comparison

// App task receives events directly
void app_task_direct(void *param) {
    event_msg_t msg;

    while (1) {
        // Block waiting for event
        if (event_bus_receive(EVENT_BUS_COMM, &msg, portMAX_DELAY)) {

            // Runs in MY task context (not event bus task)
            switch (msg.type) {
                case EVT_GSM_CONNECTED:
                    // Can block!
                    ntrip_connect();
                    vTaskDelay(1000);
                    break;

                case EVT_GPS_UPDATE:
                    // Can do heavy processing
                    save_to_sd(&msg);
                    update_display(&msg);
                    break;
            }

            event_bus_release(&msg);
        }
    }
}

/**
 * Pros:
 * - Runs in own task (own priority, own stack)
 * - Can block (delay, queue, mutex)
 * - Easy to debug (simple stack trace)
 * - Flow control (can process at own pace)
 *
 * Cons:
 * - 1:1 only (one receiver per event)
 * - Needs extra queue per task
 * - More memory usage
 */

#endif

/* ==================== Pattern 3: Hybrid (Recommended) ==================== */

/**
 * Best of both worlds:
 * - Event bus does broadcast (1:N)
 * - Callback forwards to task queue (fast, non-blocking)
 * - Task processes from queue (own context, can block)
 */

// App module with its own queue and task
typedef struct {
    QueueHandle_t event_queue;
    TaskHandle_t task_handle;
} app_module_t;

static app_module_t g_app_module;

/**
 * @brief Event forwarder callback (runs in event bus task)
 *
 * This is FAST - just copies to queue and returns.
 * No blocking, no heavy processing.
 */
static void app_event_forwarder(const event_msg_t *msg) {
    // Copy event to app's queue (non-blocking!)
    event_msg_t copy = *msg;

    if (xQueueSend(g_app_module.event_queue, &copy, 0) != pdTRUE) {
        // Queue full - could log, but don't block!
    }
}

/**
 * @brief App task (processes events from its own queue)
 *
 * This runs in APP task context, can do heavy processing.
 */
static void app_task_hybrid(void *param) {
    event_msg_t msg;

    while (1) {
        // Block on OWN queue (not event bus queue)
        if (xQueueReceive(g_app_module.event_queue, &msg, portMAX_DELAY) == pdTRUE) {

            // Runs in MY task context
            // Can block, can do heavy processing
            switch (msg.type) {
                case EVT_GSM_CONNECTED:
                    printf("[App] GSM connected, starting NTRIP...\n");

                    // Can block!
                    ntrip_client_connect();
                    vTaskDelay(pdMS_TO_TICKS(1000));

                    printf("[App] NTRIP started\n");
                    break;

                case EVT_GPS_UPDATE: {
                    const gps_data_t *gps = (const gps_data_t*)msg.data;

                    // Heavy processing OK
                    update_display(gps);
                    save_to_file(gps);  // SD card write (blocks)
                    send_to_server(gps);  // Network (blocks)

                    break;
                }

                case EVT_COMM_GSM_DATA_RX:
                    // Process server data
                    process_ntrip_data(msg.data, msg.size);
                    break;
            }
        }
    }
}

/**
 * @brief Initialize app module with hybrid pattern
 */
static inline void app_module_init_hybrid(void) {
    // 1. Create app's own queue
    g_app_module.event_queue = xQueueCreate(20, sizeof(event_msg_t));

    // 2. Subscribe to event bus (callback forwards to queue)
    SUBSCRIBE_COMM(0, app_event_forwarder);
    SUBSCRIBE_SENSOR(0, app_event_forwarder);

    // 3. Create app task (processes from queue)
    xTaskCreate(app_task_hybrid, "app", 4096, NULL, 3, &g_app_module.task_handle);
}

/**
 * Pros:
 * - Still supports 1:N broadcast (other modules can also subscribe)
 * - Callback is fast (just queue copy)
 * - Processing in own task (can block)
 * - Best of both worlds!
 *
 * Cons:
 * - Slightly more complex
 * - Extra queue per module (but needed anyway for task)
 */

/* ==================== Comparison Table ==================== */

/**
 * +------------------+-------------+---------------+-------------+
 * |                  | Callback    | Direct Recv   | Hybrid      |
 * +------------------+-------------+---------------+-------------+
 * | 1:N Broadcast    | ✅ Yes      | ❌ No         | ✅ Yes      |
 * | Can Block        | ❌ No       | ✅ Yes        | ✅ Yes      |
 * | Own Task Context | ❌ No       | ✅ Yes        | ✅ Yes      |
 * | Easy Debug       | ❌ No       | ✅ Yes        | ✅ Yes      |
 * | Memory Usage     | ✅ Low      | ❌ High       | ⚠️  Medium  |
 * | Complexity       | ✅ Simple   | ⚠️  Medium    | ⚠️  Medium  |
 * +------------------+-------------+---------------+-------------+
 */

/* ==================== Real World Example ==================== */

/**
 * Typical system with GSM, GPS, and app:
 */

// Logger: Pure callback (fast, lightweight)
void logger_init(void) {
    SUBSCRIBE_COMM(0, logger_handler);
    SUBSCRIBE_SENSOR(0, logger_handler);
}

void logger_handler(const event_msg_t *msg) {
    log_write(msg->timestamp, msg->type, msg->data, msg->size);
}

// App: Hybrid (heavy processing)
void app_init(void) {
    app_module_init_hybrid();  // Creates queue + task + subscribes
}

// LED: Pure callback (fast)
void led_init(void) {
    SUBSCRIBE_COMM((1 << EVT_GSM_CONNECTED) | (1 << EVT_GPS_FIX), led_handler);
}

void led_handler(const event_msg_t *msg) {
    switch (msg->type) {
        case EVT_GSM_CONNECTED:
            led_set(LED_GSM, true);
            break;
        case EVT_GPS_FIX:
            led_set(LED_GPS, true);
            break;
    }
}

/* ==================== Recommendation ==================== */

/**
 * For your use case (GSM, GPS, LoRa control):
 *
 * Use HYBRID pattern:
 *
 * 1. Lightweight modules (logger, LED, stats):
 *    - Use pure callback
 *    - Fast, non-blocking handlers
 *
 * 2. App module (NTRIP, data processing):
 *    - Use hybrid pattern
 *    - Callback forwards to own queue
 *    - Task processes with blocking allowed
 *
 * 3. GSM/GPS modules:
 *    - Publish only (don't subscribe)
 *    - Already have own tasks/queues
 *
 * This gives you:
 * - Broadcast capability (multiple listeners)
 * - Heavy processing in app task
 * - Clean separation of concerns
 */

#endif /* EVENT_BUS_RECEIVE_PATTERN_H */
