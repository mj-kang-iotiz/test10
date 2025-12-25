/**
 * @file event_bus.h
 * @brief Event Bus System with Registry Pattern
 *
 * Thread-safe event bus implementation using FreeRTOS queues and single-linked lists.
 * Supports publish-subscribe pattern with dynamic subscriber management.
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

/* Event message structure */
typedef struct {
    uint32_t type;          // Event type ID
    uint32_t timestamp;     // Tick count when event was published
    void *data;             // Pointer to event data (dynamically allocated)
    size_t size;            // Size of event data
} event_msg_t;

/* Event handler callback type */
typedef void (*event_handler_t)(const event_msg_t *msg);

/* Opaque event bus type */
typedef struct event_bus_s event_bus_t;

/* Subscriber node (single-linked list) */
typedef struct subscriber_node_s {
    uint32_t event_mask;                    // Bitmask of subscribed event types (0 = subscribe all)
    event_handler_t handler;                // Handler callback function
    struct subscriber_node_s *next;         // Next subscriber in list
} subscriber_node_t;

/* Event bus structure */
struct event_bus_s {
    const char *name;                       // Bus name for identification
    QueueHandle_t queue;                    // FreeRTOS queue for event messages
    subscriber_node_t *subscribers;         // Head of subscriber linked list
    SemaphoreHandle_t sub_mutex;            // Mutex for subscriber list
    TaskHandle_t dispatch_task;             // Dispatch task handle
    bool running;                           // Dispatch task running flag
    uint32_t queue_depth;                   // Maximum queue depth
};

/**
 * @brief Create a new event bus instance
 *
 * @param name Unique name for this bus (used for registry)
 * @param queue_depth Maximum number of events in queue
 * @param task_priority Priority for dispatch task
 * @return event_bus_t* Pointer to created bus, NULL on failure
 */
event_bus_t* event_bus_create(const char *name, uint32_t queue_depth, uint32_t task_priority);

/**
 * @brief Destroy an event bus instance
 *
 * @param bus Event bus to destroy
 */
void event_bus_destroy(event_bus_t *bus);

/**
 * @brief Subscribe to events on the bus
 *
 * @param bus Event bus
 * @param event_mask Bitmask of event types to subscribe to (0 = all events)
 * @param handler Event handler callback
 * @return true Success
 * @return false Failure
 */
bool event_bus_subscribe(event_bus_t *bus, uint32_t event_mask, event_handler_t handler);

/**
 * @brief Unsubscribe from events
 *
 * @param bus Event bus
 * @param handler Handler to unsubscribe
 * @return true Success
 * @return false Failure (handler not found)
 */
bool event_bus_unsubscribe(event_bus_t *bus, event_handler_t handler);

/**
 * @brief Publish an event to the bus
 *
 * The event data will be copied and queued for dispatch.
 *
 * @param bus Event bus
 * @param type Event type ID
 * @param data Pointer to event data (will be copied)
 * @param size Size of event data
 * @return true Event queued successfully
 * @return false Queue full or allocation failed
 */
bool event_bus_publish(event_bus_t *bus, uint32_t type, const void *data, size_t size);

/**
 * @brief Start the dispatch task
 *
 * @param bus Event bus
 * @return true Started successfully
 * @return false Already running or failed
 */
bool event_bus_start(event_bus_t *bus);

/**
 * @brief Stop the dispatch task
 *
 * @param bus Event bus
 */
void event_bus_stop(event_bus_t *bus);

/* ==================== Registry Functions ==================== */

/**
 * @brief Register an event bus in the global registry
 *
 * @param name Unique name for the bus
 * @param bus Event bus instance
 * @return true Registered successfully
 * @return false Registry full or name already exists
 */
bool event_bus_register(const char *name, event_bus_t *bus);

/**
 * @brief Unregister an event bus from the registry
 *
 * @param name Bus name to unregister
 * @return true Unregistered successfully
 * @return false Not found
 */
bool event_bus_unregister(const char *name);

/**
 * @brief Get an event bus from the registry by name
 *
 * @param name Bus name
 * @return event_bus_t* Bus instance, NULL if not found
 */
event_bus_t* event_bus_get_instance(const char *name);

/**
 * @brief Get the default event bus (convenience function)
 *
 * Returns the bus registered as "default".
 *
 * @return event_bus_t* Default bus, NULL if not registered
 */
event_bus_t* event_bus_default(void);

#endif /* EVENT_BUS_H */
