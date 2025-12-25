/**
 * @file event_bus.c
 * @brief Event Bus System Implementation
 */

#include "event_bus.h"
#include <string.h>
#include <stdlib.h>

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

/* Forward declaration */
static void event_dispatch_task(void *pvParameter);

/**
 * @brief Initialize registry mutex (called once)
 */
static void registry_init(void) {
    if (g_registry_mutex == NULL) {
        g_registry_mutex = xSemaphoreCreateMutex();
    }
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
    if (!name) {
        return false;
    }

    if (!g_registry_mutex) {
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
    bus->subscribers = NULL;
    bus->running = false;

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
                if (msg->data) {
                    vPortFree(msg->data);
                }
                vPortFree(msg);
            }
        }
        vQueueDelete(bus->queue);
    }

    // Delete all subscribers
    if (bus->sub_mutex) {
        xSemaphoreTake(bus->sub_mutex, portMAX_DELAY);

        subscriber_node_t *current = bus->subscribers;
        while (current) {
            subscriber_node_t *next = current->next;
            vPortFree(current);
            current = next;
        }

        xSemaphoreGive(bus->sub_mutex);
        vSemaphoreDelete(bus->sub_mutex);
    }

    // Free bus structure
    vPortFree(bus);
}

bool event_bus_subscribe(event_bus_t *bus, uint32_t event_mask, event_handler_t handler) {
    if (!bus || !handler) {
        return false;
    }

    // Allocate new subscriber node
    subscriber_node_t *node = (subscriber_node_t*)pvPortMalloc(sizeof(subscriber_node_t));
    if (!node) {
        return false;
    }

    node->event_mask = event_mask;
    node->handler = handler;
    node->next = NULL;

    // Add to subscriber list (prepend)
    xSemaphoreTake(bus->sub_mutex, portMAX_DELAY);
    node->next = bus->subscribers;
    bus->subscribers = node;
    xSemaphoreGive(bus->sub_mutex);

    return true;
}

bool event_bus_unsubscribe(event_bus_t *bus, event_handler_t handler) {
    if (!bus || !handler) {
        return false;
    }

    xSemaphoreTake(bus->sub_mutex, portMAX_DELAY);

    subscriber_node_t *current = bus->subscribers;
    subscriber_node_t *prev = NULL;

    while (current) {
        if (current->handler == handler) {
            // Found - remove from list
            if (prev) {
                prev->next = current->next;
            } else {
                bus->subscribers = current->next;
            }

            vPortFree(current);
            xSemaphoreGive(bus->sub_mutex);
            return true;
        }

        prev = current;
        current = current->next;
    }

    xSemaphoreGive(bus->sub_mutex);
    return false;  // Not found
}

bool event_bus_publish(event_bus_t *bus, uint32_t type, const void *data, size_t size) {
    if (!bus) {
        return false;
    }

    // Allocate event message
    event_msg_t *msg = (event_msg_t*)pvPortMalloc(sizeof(event_msg_t));
    if (!msg) {
        return false;
    }

    msg->type = type;
    msg->timestamp = xTaskGetTickCount();
    msg->size = size;

    // Copy event data
    if (data && size > 0) {
        msg->data = pvPortMalloc(size);
        if (!msg->data) {
            vPortFree(msg);
            return false;
        }
        memcpy(msg->data, data, size);
    } else {
        msg->data = NULL;
    }

    // Queue the message (pointer only)
    if (xQueueSend(bus->queue, &msg, 0) != pdTRUE) {
        // Queue full
        if (msg->data) {
            vPortFree(msg->data);
        }
        vPortFree(msg);
        return false;
    }

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

            subscriber_node_t *sub = bus->subscribers;
            while (sub) {
                // Check if subscriber is interested in this event type
                if (sub->event_mask == 0 || (sub->event_mask & (1 << msg->type))) {
                    // Call handler
                    if (sub->handler) {
                        sub->handler(msg);
                    }
                }
                sub = sub->next;
            }

            xSemaphoreGive(bus->sub_mutex);

            // Free message
            if (msg->data) {
                vPortFree(msg->data);
            }
            vPortFree(msg);
        }
    }

    vTaskDelete(NULL);
}
