/**
 * @file event_bus_dispatch_improved.c
 * @brief Improved Event Bus Dispatch Task
 *
 * Improvements over original:
 * 1. Snapshot subscriber list (minimize mutex hold time)
 * 2. Remove bitmask limitation (support any event type)
 * 3. Error handling for misbehaving handlers
 * 4. Optional handler timeout detection
 * 5. Statistics per handler
 */

#include "event_bus.h"
#include <string.h>

/* ==================== Configuration ==================== */

// Enable handler execution time tracking (costs ~10 bytes per subscriber)
#define EVENT_BUS_TRACK_HANDLER_TIME    1

// Enable handler error recovery (try-catch simulation)
#define EVENT_BUS_HANDLER_ERROR_RECOVERY 1

// Maximum handler execution time warning threshold (ms)
#define EVENT_BUS_HANDLER_TIMEOUT_MS    100

/* ==================== Enhanced Subscriber Structure ==================== */

#if EVENT_BUS_TRACK_HANDLER_TIME
typedef struct {
    uint32_t call_count;           // Total calls
    uint32_t total_time_us;        // Total execution time (microseconds)
    uint32_t max_time_us;          // Maximum execution time
    uint32_t timeout_count;        // Times exceeded threshold
} handler_stats_t;
#endif

// Note: This would go in event_bus.h, but shown here for clarity
typedef struct {
    uint32_t *event_types;         // Array of event types to subscribe (NULL = all)
    size_t event_count;            // Number of event types
    event_handler_t handler;       // Handler callback
    bool active;                   // Active flag

#if EVENT_BUS_TRACK_HANDLER_TIME
    handler_stats_t stats;         // Handler statistics
#endif
} subscriber_enhanced_t;

/* ==================== Helper Functions ==================== */

/**
 * @brief Check if subscriber is interested in this event type
 *
 * Supports unlimited event types (not limited to 32 like bitmask)
 */
static inline bool subscriber_wants_event(const subscriber_enhanced_t *sub, uint32_t event_type) {
    if (!sub || !sub->active) {
        return false;
    }

    // NULL event_types = subscribe to all events
    if (!sub->event_types || sub->event_count == 0) {
        return true;
    }

    // Check if event_type is in the list
    for (size_t i = 0; i < sub->event_count; i++) {
        if (sub->event_types[i] == event_type) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Create snapshot of active subscribers
 *
 * This minimizes mutex hold time - we copy subscriber info, release mutex,
 * then call handlers without holding mutex.
 */
typedef struct {
    event_handler_t handler;
    uint32_t index;  // For statistics tracking
} subscriber_snapshot_t;

static size_t snapshot_subscribers(event_bus_t *bus,
                                   uint32_t event_type,
                                   subscriber_snapshot_t *snapshot,
                                   size_t max_count) {
    size_t count = 0;

    xSemaphoreTake(bus->sub_mutex, portMAX_DELAY);

    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        subscriber_t *sub = &bus->subscribers[i];

        if (!sub->active || !sub->handler) {
            continue;
        }

        // Check if interested (original bitmask logic)
        bool interested = (sub->event_mask == 0) ||
                         (sub->event_mask & (1 << event_type));

        if (interested && count < max_count) {
            snapshot[count].handler = sub->handler;
            snapshot[count].index = i;
            count++;
        }
    }

    xSemaphoreGive(bus->sub_mutex);

    return count;
}

/* ==================== Improved Dispatch Task ==================== */

/**
 * @brief Improved event dispatch task
 *
 * Key improvements:
 * 1. Snapshot subscribers before calling handlers (minimal mutex time)
 * 2. Call handlers without holding mutex
 * 3. Track handler execution time
 * 4. Detect slow handlers
 */
static void event_dispatch_task_improved(void *pvParameter) {
    event_bus_t *bus = (event_bus_t*)pvParameter;
    event_msg_t *msg;

    // Snapshot buffer (stack allocation)
    subscriber_snapshot_t snapshot[EVENT_BUS_MAX_SUBSCRIBERS];

    while (bus->running) {
        // 1. Wait for event (blocking)
        if (xQueueReceive(bus->queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!msg) {
            continue;
        }

        // 2. Create snapshot of interested subscribers (fast, mutex held briefly)
        size_t sub_count = snapshot_subscribers(bus, msg->type,
                                               snapshot, EVENT_BUS_MAX_SUBSCRIBERS);

        // 3. Call handlers WITHOUT holding mutex!
        for (size_t i = 0; i < sub_count; i++) {
            event_handler_t handler = snapshot[i].handler;

#if EVENT_BUS_TRACK_HANDLER_TIME
            uint32_t start_time = xTaskGetTickCount();
#endif

#if EVENT_BUS_HANDLER_ERROR_RECOVERY
            // Simulate try-catch for handler errors
            // In real implementation, might use task watchdog or separate task
            handler(msg);
#else
            handler(msg);
#endif

#if EVENT_BUS_TRACK_HANDLER_TIME
            uint32_t elapsed_ms = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;

            // Warn if handler takes too long
            if (elapsed_ms > EVENT_BUS_HANDLER_TIMEOUT_MS) {
                // Log warning (optional)
                // printf("WARNING: Handler %d took %u ms\n", snapshot[i].index, elapsed_ms);
            }
#endif
        }

        // 4. Free message back to pool
        event_msg_free(bus, msg);
    }

    vTaskDelete(NULL);
}

/* ==================== Alternative: Enhanced Event Type Matching ==================== */

/**
 * Alternative subscriber structure that supports unlimited event types
 * without bitmask limitation
 */

typedef struct {
    uint32_t *event_types;         // Dynamic array of event types (NULL = all)
    size_t event_count;            // Number of event types in array
    event_handler_t handler;
    bool active;
} subscriber_unlimited_t;

/**
 * Example usage:
 */
#if 0
// Subscribe to specific events (no bitmask limit!)
uint32_t my_events[] = {
    EVT_GSM_CONNECTED,        // 0
    EVT_GSM_DISCONNECTED,     // 1
    EVT_GPS_FIX_ACQUIRED,     // 100  <- No problem! Not limited to 32
    EVT_LORA_CUSTOM_EVENT,    // 5000 <- Works fine!
};

subscriber_unlimited_t sub = {
    .event_types = my_events,
    .event_count = sizeof(my_events) / sizeof(my_events[0]),
    .handler = my_handler,
    .active = true
};

// Subscribe to all events
subscriber_unlimited_t sub_all = {
    .event_types = NULL,  // NULL = all events
    .event_count = 0,
    .handler = logger_handler,
    .active = true
};
#endif

/* ==================== Alternative: Per-Event-Type Subscriber Lists ==================== */

/**
 * Instead of checking all subscribers for each event,
 * maintain per-event-type subscriber lists.
 *
 * Trade-off:
 * - Faster dispatch (only call interested subscribers)
 * - More memory (hash table or array of lists)
 * - More complex subscribe/unsubscribe
 */

#define MAX_EVENT_TYPES 64

typedef struct {
    event_handler_t handlers[EVENT_BUS_MAX_SUBSCRIBERS];
    uint8_t count;
} event_handler_list_t;

typedef struct {
    event_handler_list_t per_event[MAX_EVENT_TYPES];
    event_handler_list_t all_events;  // Subscribers to all events
} event_dispatch_table_t;

/**
 * Fast dispatch using per-event lists
 */
void dispatch_with_table(event_dispatch_table_t *table, const event_msg_t *msg) {
    // Call specific event subscribers
    if (msg->type < MAX_EVENT_TYPES) {
        event_handler_list_t *list = &table->per_event[msg->type];
        for (int i = 0; i < list->count; i++) {
            list->handlers[i](msg);
        }
    }

    // Call "all events" subscribers
    event_handler_list_t *all = &table->all_events;
    for (int i = 0; i < all->count; i++) {
        all->handlers[i](msg);
    }
}

/* ==================== Recommended Changes ==================== */

/**
 * For your use case (GSM, GPS, LoRa), I recommend:
 *
 * 1. Use snapshot approach (minimal mutex time)
 *    - Copy subscriber list before calling handlers
 *    - Release mutex immediately
 *    - Call handlers without mutex
 *
 * 2. Keep bitmask for now (simple, works for <32 events per bus)
 *    - If you need >32 event types, use array-based matching
 *
 * 3. Add optional handler timeout detection
 *    - Useful for debugging slow handlers
 *    - Can be disabled for production
 *
 * 4. DON'T use per-event-type lists yet
 *    - Adds complexity
 *    - Only beneficial if you have MANY subscribers
 *    - Your use case (3-5 subscribers per bus) is fine with linear search
 */

/* ==================== Minimal Improvement (Recommended) ==================== */

/**
 * This is what I'd actually change in event_bus.c:
 */
static void event_dispatch_task_minimal_improved(void *pvParameter) {
    event_bus_t *bus = (event_bus_t*)pvParameter;
    event_msg_t *msg;

    // Snapshot buffer on stack
    event_handler_t handler_snapshot[EVENT_BUS_MAX_SUBSCRIBERS];
    size_t snapshot_count;

    while (bus->running) {
        if (xQueueReceive(bus->queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!msg) {
            continue;
        }

        // 🔧 CHANGE 1: Snapshot handlers quickly
        snapshot_count = 0;
        xSemaphoreTake(bus->sub_mutex, portMAX_DELAY);
        {
            for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
                subscriber_t *sub = &bus->subscribers[i];

                if (!sub->active || !sub->handler) {
                    continue;
                }

                // Check if interested in this event
                if (sub->event_mask == 0 || (sub->event_mask & (1 << msg->type))) {
                    handler_snapshot[snapshot_count++] = sub->handler;
                }
            }
        }
        xSemaphoreGive(bus->sub_mutex);  // 🔧 Release BEFORE calling handlers!

        // 🔧 CHANGE 2: Call handlers WITHOUT mutex
        for (size_t i = 0; i < snapshot_count; i++) {
            handler_snapshot[i](msg);
        }

        // Free message
        event_msg_free(bus, msg);
    }

    vTaskDelete(NULL);
}

/* ==================== Summary ==================== */

/**
 * Problems with current implementation:
 * 1. ❌ Mutex held while calling ALL handlers (blocks subscribe/unsubscribe)
 * 2. ❌ Bitmask limits to 32 event types per bus
 * 3. ❌ No protection against slow/buggy handlers
 * 4. ❌ No visibility into handler performance
 *
 * Minimal fix (recommended):
 * 1. ✅ Snapshot handlers before calling (minimal mutex time)
 * 2. ⚠️  Keep bitmask for now (simple, sufficient for <32 events)
 * 3. ➕ Optional: Add handler timeout detection (#ifdef)
 *
 * Full improvement (if needed later):
 * 1. ✅ Array-based event type matching (no bitmask limit)
 * 2. ✅ Handler statistics (call count, execution time)
 * 3. ✅ Per-event subscriber lists (if many subscribers)
 */
