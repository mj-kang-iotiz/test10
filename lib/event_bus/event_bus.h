/**
 * @file event_bus.h
 * @brief Event Bus System with Registry Pattern (Full Static Version)
 *
 * Thread-safe event bus implementation using FreeRTOS queues and static allocation.
 * Supports publish-subscribe pattern with object pool for events.
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

/* Configuration */
#define EVENT_BUS_MAX_SUBSCRIBERS   16      // Maximum subscribers per bus
#define EVENT_MSG_POOL_SIZE         20      // Total event message pool size
#define EVENT_DATA_MAX_SIZE         512     // Maximum event data size (bytes)

/* Event message structure (static buffer) */
typedef struct {
    uint32_t type;                          // Event type ID
    uint32_t timestamp;                     // Tick count when published
    uint8_t data[EVENT_DATA_MAX_SIZE];      // Event data (static buffer)
    size_t size;                            // Actual data size used
    bool in_use;                            // Allocation flag
} event_msg_t;

/* Event handler callback type */
typedef void (*event_handler_t)(const event_msg_t *msg);

/* Subscriber structure (static) */
typedef struct {
    uint32_t event_mask;                    // Bitmask of subscribed events (0 = all)
    event_handler_t handler;                // Handler callback
    bool active;                            // Active flag
} subscriber_t;

/* Event bus structure */
typedef struct {
    const char *name;                       // Bus name
    QueueHandle_t queue;                    // FreeRTOS queue
    subscriber_t subscribers[EVENT_BUS_MAX_SUBSCRIBERS];  // Static subscriber array
    SemaphoreHandle_t sub_mutex;            // Subscriber list mutex
    TaskHandle_t dispatch_task;             // Dispatch task handle
    bool running;                           // Running flag
    uint32_t queue_depth;                   // Queue depth

    /* Statistics */
    uint32_t sub_count;                     // Active subscriber count
    uint32_t publish_success;               // Successful publishes
    uint32_t publish_failed;                // Failed publishes
} event_bus_t;

/* Event message pool (global static) */
typedef struct {
    event_msg_t pool[EVENT_MSG_POOL_SIZE];  // Static event pool
    SemaphoreHandle_t mutex;                // Pool mutex
    uint32_t allocated;                     // Currently allocated count
    uint32_t peak;                          // Peak allocation
    uint32_t failures;                      // Allocation failures
} event_msg_pool_t;

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
 * @return false Failure (bus full)
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
 * The event data will be copied to the static pool.
 *
 * @param bus Event bus
 * @param type Event type ID
 * @param data Pointer to event data (will be copied)
 * @param size Size of event data (must be <= EVENT_DATA_MAX_SIZE)
 * @return true Event queued successfully
 * @return false Queue full, pool exhausted, or size too large
 */
bool event_bus_publish(event_bus_t *bus, uint32_t type, const void *data, size_t size);

/**
 * @brief Start the dispatch task (called automatically in create)
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

/**
 * @brief Get event bus statistics
 *
 * @param bus Event bus
 * @param sub_count Output: active subscriber count
 * @param publish_success Output: successful publish count
 * @param publish_failed Output: failed publish count
 */
void event_bus_get_stats(event_bus_t *bus, uint32_t *sub_count,
                         uint32_t *publish_success, uint32_t *publish_failed);

/**
 * @brief Get event pool statistics
 *
 * @param allocated Output: currently allocated messages
 * @param peak Output: peak allocation
 * @param failures Output: allocation failures
 */
void event_bus_get_pool_stats(uint32_t *allocated, uint32_t *peak, uint32_t *failures);

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
