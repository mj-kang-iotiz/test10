/**
 * @file event_bus_config.c
 * @brief Event Bus Configuration Implementation
 */

#include "event_bus_config.h"
#include <stdio.h>

/* ==================== Internal State ==================== */

// Cached bus pointers (initialized once, then O(1) access)
static event_bus_t *g_buses[BUS_COUNT] = {NULL};
static bool g_initialized = false;
static SemaphoreHandle_t g_init_mutex = NULL;

/* ==================== Initialization ==================== */

/**
 * @brief Initialize all event buses from configuration
 */
void event_bus_init_all(void) {
    if (g_initialized) {
        return;  // Already initialized
    }

    // Create init mutex if needed
    if (g_init_mutex == NULL) {
        g_init_mutex = xSemaphoreCreateMutex();
    }

    xSemaphoreTake(g_init_mutex, portMAX_DELAY);

    // Double-check after acquiring mutex
    if (g_initialized) {
        xSemaphoreGive(g_init_mutex);
        return;
    }

    // Create all buses from configuration
    for (int i = 0; i < BUS_COUNT; i++) {
        const bus_config_t *cfg = &BUS_CONFIGS[i];

        g_buses[i] = event_bus_create(cfg->name, cfg->queue_depth, cfg->priority);

        if (!g_buses[i]) {
            printf("ERROR: Failed to create bus '%s'\n", cfg->name);
        } else {
            printf("Created bus '%s' (queue=%u, prio=%u)\n",
                   cfg->name, cfg->queue_depth, cfg->priority);
        }
    }

    g_initialized = true;
    xSemaphoreGive(g_init_mutex);
}

/**
 * @brief Get event bus by ID (O(1) access with lazy init)
 */
event_bus_t* event_bus_get(bus_id_t id) {
    if (id >= BUS_COUNT) {
        return NULL;  // Invalid ID
    }

    // Lazy initialization on first access
    if (!g_initialized) {
        event_bus_init_all();
    }

    // Direct array access - O(1)!
    return g_buses[id];
}

/**
 * @brief Print statistics for a specific bus
 */
void event_bus_print_stats(bus_id_t id) {
    event_bus_t *bus = event_bus_get(id);
    if (!bus) {
        printf("Bus %d not found\n", id);
        return;
    }

    uint32_t sub_count, pub_ok, pub_fail, pool_alloc, pool_peak, pool_fail;
    event_bus_get_stats(bus, &sub_count, &pub_ok, &pub_fail,
                       &pool_alloc, &pool_peak, &pool_fail);

    const bus_config_t *cfg = &BUS_CONFIGS[id];
    printf("\n=== Bus '%s' Statistics ===\n", cfg->name);
    printf("  Subscribers:     %u\n", sub_count);
    printf("  Publish success: %u\n", pub_ok);
    printf("  Publish failed:  %u\n", pub_fail);
    printf("  Pool allocated:  %u / %d\n", pool_alloc, EVENT_MSG_POOL_SIZE);
    printf("  Pool peak:       %u\n", pool_peak);
    printf("  Pool failures:   %u\n", pool_fail);
}

/**
 * @brief Print statistics for all buses
 */
void event_bus_print_all_stats(void) {
    for (int i = 0; i < BUS_COUNT; i++) {
        event_bus_print_stats(i);
    }
}

/**
 * @brief Get event bus by name (O(1) with pointer comparison!)
 *
 * Uses string interning - only works with strings from BUS_CONFIGS.
 */
event_bus_t* event_bus_get_by_name(const char *name) {
    if (!g_initialized) {
        event_bus_init_all();
    }

    // Fast pointer comparison (not strcmp!)
    for (int i = 0; i < BUS_COUNT; i++) {
        if (BUS_CONFIGS[i].name == name) {  // O(1) pointer compare!
            return g_buses[i];
        }
    }

    // Fallback to strcmp for runtime strings
    for (int i = 0; i < BUS_COUNT; i++) {
        if (strcmp(BUS_CONFIGS[i].name, name) == 0) {
            return g_buses[i];
        }
    }

    return NULL;
}
