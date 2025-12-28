/**
 * @file event_bus.c
 * @brief Event Bus System Implementation (Full Static Version)
 */

#include "event_bus.h"
#include <string.h>

/* Registry configuration */
#define MAX_EVENT_BUSES 5

/* Registry entry */
typedef struct {
    const char *name;
    event_bus_t *bus;
} event_bus_registry_entry_t;

/* Global registry */
static event_bus_registry_entry_t g_registry[MAX_EVENT_BUSES] = {0};
static SemaphoreHandle_t g_registry_mutex = NULL;

/* Forward declarations */
static void event_dispatch_task(void *pvParameter);
static event_msg_t* event_msg_alloc(event_bus_t *bus);
static void event_msg_free(event_bus_t *bus, event_msg_t *msg);

/**
 * @brief Initialize registry mutex (called once)
 */
static void registry_init(void) {
    if (g_registry_mutex == NULL) {
        g_registry_mutex = xSemaphoreCreateMutex();
    }
}

/**
 * @brief Allocate an event message from the per-bus pool
 *
 * @param bus Event bus
 * @return event_msg_t* Allocated message, NULL if pool exhausted
 */
static event_msg_t* event_msg_alloc(event_bus_t *bus) {
    if (!bus || !bus->pool_mutex) {
        return NULL;
    }

    xSemaphoreTake(bus->pool_mutex, portMAX_DELAY);

    // Find free message in this bus's pool
    event_msg_t *msg = NULL;
    for (int i = 0; i < EVENT_MSG_POOL_SIZE; i++) {
        if (!bus->msg_pool[i].in_use) {
            msg = &bus->msg_pool[i];
            msg->in_use = true;
            bus->pool_allocated++;

            // Update peak
            if (bus->pool_allocated > bus->pool_peak) {
                bus->pool_peak = bus->pool_allocated;
            }
            break;
        }
    }

    if (!msg) {
        bus->pool_failures++;
    }

    xSemaphoreGive(bus->pool_mutex);
    return msg;
}

/**
 * @brief Free an event message back to the per-bus pool
 *
 * @param bus Event bus
 * @param msg Message to free
 */
static void event_msg_free(event_bus_t *bus, event_msg_t *msg) {
    if (!bus || !msg || !bus->pool_mutex) {
        return;
    }

    xSemaphoreTake(bus->pool_mutex, portMAX_DELAY);

    msg->in_use = false;
    bus->pool_allocated--;

    xSemaphoreGive(bus->pool_mutex);
}

/* ==================== Registry Functions ==================== */

bool event_bus_register(const char *name, event_bus_t *bus) {
    if (!name || !bus) {
        return false;
    }

    registry_init();

    xSemaphoreTake(g_registry_mutex, portMAX_DELAY);

    // Check if name already exists
    for (int i = 0; i < MAX_EVENT_BUSES; i++) {
        if (g_registry[i].name && strcmp(g_registry[i].name, name) == 0) {
            xSemaphoreGive(g_registry_mutex);
            return false;  // Name already registered
        }
    }

    // Find empty slot
    for (int i = 0; i < MAX_EVENT_BUSES; i++) {
        if (g_registry[i].bus == NULL) {
            g_registry[i].name = name;
            g_registry[i].bus = bus;
            xSemaphoreGive(g_registry_mutex);
            return true;
        }
    }

    xSemaphoreGive(g_registry_mutex);
    return false;  // Registry full
}

bool event_bus_unregister(const char *name) {
    if (!name || !g_registry_mutex) {
        return false;
    }

    xSemaphoreTake(g_registry_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_EVENT_BUSES; i++) {
        if (g_registry[i].name && strcmp(g_registry[i].name, name) == 0) {
            g_registry[i].name = NULL;
            g_registry[i].bus = NULL;
            xSemaphoreGive(g_registry_mutex);
            return true;
        }
    }

    xSemaphoreGive(g_registry_mutex);
    return false;  // Not found
}

event_bus_t* event_bus_get_instance(const char *name) {
    if (!name || !g_registry_mutex) {
        return NULL;
    }

    xSemaphoreTake(g_registry_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_EVENT_BUSES; i++) {
        if (g_registry[i].name && strcmp(g_registry[i].name, name) == 0) {
            event_bus_t *bus = g_registry[i].bus;
            xSemaphoreGive(g_registry_mutex);
            return bus;
        }
    }

    xSemaphoreGive(g_registry_mutex);
    return NULL;  // Not found
}

event_bus_t* event_bus_default(void) {
    return event_bus_get_instance("default");
}

/* ==================== Event Bus Functions ==================== */

event_bus_t* event_bus_create(const char *name, uint32_t queue_depth, uint32_t task_priority) {
    if (!name || queue_depth == 0) {
        return NULL;
    }

    // Allocate bus structure
    event_bus_t *bus = (event_bus_t*)pvPortMalloc(sizeof(event_bus_t));
    if (!bus) {
        return NULL;
    }

    memset(bus, 0, sizeof(event_bus_t));

    // Initialize fields
    bus->name = name;
    bus->queue_depth = queue_depth;
    bus->running = false;
    bus->sub_count = 0;
    bus->publish_success = 0;
    bus->publish_failed = 0;
    bus->pool_allocated = 0;
    bus->pool_peak = 0;
    bus->pool_failures = 0;

    // Initialize subscriber array
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        bus->subscribers[i].active = false;
        bus->subscribers[i].handler = NULL;
        bus->subscribers[i].event_mask = 0;
    }

    // Initialize per-bus message pool
    for (int i = 0; i < EVENT_MSG_POOL_SIZE; i++) {
        bus->msg_pool[i].in_use = false;
    }

    // Create queue (stores pointers to event_msg_t)
    bus->queue = xQueueCreate(queue_depth, sizeof(event_msg_t*));
    if (!bus->queue) {
        vPortFree(bus);
        return NULL;
    }

    // Create mutex for subscriber list
    bus->sub_mutex = xSemaphoreCreateMutex();
    if (!bus->sub_mutex) {
        vQueueDelete(bus->queue);
        vPortFree(bus);
        return NULL;
    }

    // Create mutex for message pool
    bus->pool_mutex = xSemaphoreCreateMutex();
    if (!bus->pool_mutex) {
        vSemaphoreDelete(bus->sub_mutex);
        vQueueDelete(bus->queue);
        vPortFree(bus);
        return NULL;
    }

    // Create dispatch task
    char task_name[16];
    snprintf(task_name, sizeof(task_name), "evbus_%s", name);

    BaseType_t ret = xTaskCreate(
        event_dispatch_task,
        task_name,
        512,  // Stack size
        bus,  // Parameter
        task_priority,
        &bus->dispatch_task
    );

    if (ret != pdPASS) {
        vSemaphoreDelete(bus->pool_mutex);
        vSemaphoreDelete(bus->sub_mutex);
        vQueueDelete(bus->queue);
        vPortFree(bus);
        return NULL;
    }

    bus->running = true;

    // Register in global registry
    event_bus_register(name, bus);

    return bus;
}

void event_bus_destroy(event_bus_t *bus) {
    if (!bus) {
        return;
    }

    // Unregister from registry
    event_bus_unregister(bus->name);

    // Stop dispatch task
    bus->running = false;
    if (bus->dispatch_task) {
        vTaskDelete(bus->dispatch_task);
        bus->dispatch_task = NULL;
    }

    // Delete queue (drain it first)
    if (bus->queue) {
        event_msg_t *msg;
        while (xQueueReceive(bus->queue, &msg, 0) == pdTRUE) {
            if (msg) {
                event_msg_free(bus, msg);
            }
        }
        vQueueDelete(bus->queue);
    }

    // Delete mutexes
    if (bus->sub_mutex) {
        vSemaphoreDelete(bus->sub_mutex);
    }
    if (bus->pool_mutex) {
        vSemaphoreDelete(bus->pool_mutex);
    }

    // Free bus structure
    vPortFree(bus);
}

bool event_bus_subscribe(event_bus_t *bus, uint32_t event_mask, event_handler_t handler) {
    if (!bus || !handler) {
        return false;
    }

    xSemaphoreTake(bus->sub_mutex, portMAX_DELAY);

    // Find empty slot
    bool subscribed = false;
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (!bus->subscribers[i].active) {
            bus->subscribers[i].event_mask = event_mask;
            bus->subscribers[i].handler = handler;
            bus->subscribers[i].active = true;
            bus->sub_count++;
            subscribed = true;
            break;
        }
    }

    xSemaphoreGive(bus->sub_mutex);
    return subscribed;
}

bool event_bus_unsubscribe(event_bus_t *bus, event_handler_t handler) {
    if (!bus || !handler) {
        return false;
    }

    xSemaphoreTake(bus->sub_mutex, portMAX_DELAY);

    bool unsubscribed = false;
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (bus->subscribers[i].active && bus->subscribers[i].handler == handler) {
            bus->subscribers[i].active = false;
            bus->subscribers[i].handler = NULL;
            bus->sub_count--;
            unsubscribed = true;
            break;
        }
    }

    xSemaphoreGive(bus->sub_mutex);
    return unsubscribed;
}

bool event_bus_publish(event_bus_t *bus, uint32_t type, const void *data, size_t size) {
    if (!bus) {
        return false;
    }

    // Check size limit
    if (size > EVENT_DATA_MAX_SIZE) {
        bus->publish_failed++;
        return false;
    }

    // Allocate message from this bus's pool
    event_msg_t *msg = event_msg_alloc(bus);
    if (!msg) {
        bus->publish_failed++;
        return false;  // Pool exhausted
    }

    // Fill message
    msg->type = type;
    msg->timestamp = xTaskGetTickCount();
    msg->size = size;

    // Copy event data to static buffer
    if (data && size > 0) {
        memcpy(msg->data, data, size);
    }

    // Queue the message (pointer only)
    if (xQueueSend(bus->queue, &msg, 0) != pdTRUE) {
        // Queue full
        event_msg_free(bus, msg);
        bus->publish_failed++;
        return false;
    }

    bus->publish_success++;
    return true;
}

bool event_bus_start(event_bus_t *bus) {
    if (!bus) {
        return false;
    }

    if (bus->running) {
        return false;  // Already running
    }

    bus->running = true;
    return true;
}

void event_bus_stop(event_bus_t *bus) {
    if (bus) {
        bus->running = false;
    }
}

void event_bus_get_stats(event_bus_t *bus,
                         uint32_t *sub_count,
                         uint32_t *publish_success,
                         uint32_t *publish_failed,
                         uint32_t *pool_allocated,
                         uint32_t *pool_peak,
                         uint32_t *pool_failures) {
    if (!bus) {
        return;
    }

    if (sub_count) *sub_count = bus->sub_count;
    if (publish_success) *publish_success = bus->publish_success;
    if (publish_failed) *publish_failed = bus->publish_failed;

    if (pool_allocated || pool_peak || pool_failures) {
        xSemaphoreTake(bus->pool_mutex, portMAX_DELAY);
        if (pool_allocated) *pool_allocated = bus->pool_allocated;
        if (pool_peak) *pool_peak = bus->pool_peak;
        if (pool_failures) *pool_failures = bus->pool_failures;
        xSemaphoreGive(bus->pool_mutex);
    }
}

/**
 * @brief Event dispatch task
 *
 * Receives events from queue and dispatches to subscribers.
 */
static void event_dispatch_task(void *pvParameter) {
    event_bus_t *bus = (event_bus_t*)pvParameter;
    event_msg_t *msg;

    while (bus->running) {
        // Wait for event (blocking)
        if (xQueueReceive(bus->queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (!msg) {
                continue;
            }

            // Dispatch to all matching subscribers
            xSemaphoreTake(bus->sub_mutex, portMAX_DELAY);

            for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
                subscriber_t *sub = &bus->subscribers[i];

                if (!sub->active) {
                    continue;
                }

                // Check if subscriber is interested in this event type
                if (sub->event_mask == 0 || (sub->event_mask & (1 << msg->type))) {
                    // Call handler
                    if (sub->handler) {
                        sub->handler(msg);
                    }
                }
            }

            xSemaphoreGive(bus->sub_mutex);

            // Free message back to this bus's pool
            event_msg_free(bus, msg);
        }
    }

    vTaskDelete(NULL);
}
