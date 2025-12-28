/**
 * @file event_bus_config.c
 * @brief Event Bus Configuration Implementation (ESP-IDF style)
 */

#include "event_bus_config.h"
#include <stdio.h>
#include <string.h>

/* ==================== Event Bus Base Definitions (ESP-IDF style) ==================== */

/**
 * Event bus base strings.
 * Their addresses are used as unique identifiers (no strcmp needed).
 */
const char EVENT_BUS_COMM[]    = "comm";
const char EVENT_BUS_SENSOR[]  = "sensor";
const char EVENT_BUS_CONTROL[] = "control";

/* ==================== Bus Configuration ==================== */

const bus_config_t g_bus_configs[BUS_ID_COUNT] = {
    [BUS_ID_COMM]    = { EVENT_BUS_COMM,    12, 4 },  // High priority
    [BUS_ID_SENSOR]  = { EVENT_BUS_SENSOR,  20, 3 },  // Medium priority
    [BUS_ID_CONTROL] = { EVENT_BUS_CONTROL,  5, 5 },  // Highest priority
};

/* ==================== Internal State ==================== */

// Cached bus pointers (initialized once, then O(1) access)
static event_bus_t *g_buses[BUS_ID_COUNT] = {NULL};
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
    for (int i = 0; i < BUS_ID_COUNT; i++) {
        const bus_config_t *cfg = &g_bus_configs[i];

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
 * @brief Get event bus by base pointer (ESP-IDF style, O(1))
 *
 * Uses pointer comparison only - no strcmp!
 */
event_bus_t* event_bus_get(const char *base) {
    if (!base) {
        return NULL;
    }

    // Lazy initialization on first access
    if (!g_initialized) {
        event_bus_init_all();
    }

    // Pointer comparison only (O(1) for each check)
    for (int i = 0; i < BUS_ID_COUNT; i++) {
        if (g_bus_configs[i].name == base) {  // Pointer comparison!
            return g_buses[i];
        }
    }

    return NULL;  // Not found
}

/**
 * @brief Print statistics for a specific bus
 */
void event_bus_print_stats(const char *base) {
    event_bus_t *bus = event_bus_get(base);
    if (!bus) {
        printf("Bus '%s' not found\n", base ? base : "NULL");
        return;
    }

    uint32_t sub_count, pub_ok, pub_fail, pool_alloc, pool_peak, pool_fail;
    event_bus_get_stats(bus, &sub_count, &pub_ok, &pub_fail,
                       &pool_alloc, &pool_peak, &pool_fail);

    printf("\n=== Bus '%s' Statistics ===\n", base);
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
    event_bus_print_stats(EVENT_BUS_COMM);
    event_bus_print_stats(EVENT_BUS_SENSOR);
    event_bus_print_stats(EVENT_BUS_CONTROL);
}
