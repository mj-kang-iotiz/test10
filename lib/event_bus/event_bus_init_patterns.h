/**
 * @file event_bus_init_patterns.h
 * @brief Event Bus Initialization Patterns for Library Integration
 *
 * Solves the problem: When to set event handlers when moving subsystems to lib/?
 *
 * Problem scenario:
 * - lib/gsm, lib/gps have their own initialization and event handlers
 * - They don't know when event bus is initialized
 * - They don't know when to subscribe
 * - Application needs to wire everything together
 *
 * This file provides proven patterns for handling initialization order.
 */

#ifndef EVENT_BUS_INIT_PATTERNS_H
#define EVENT_BUS_INIT_PATTERNS_H

#include "event_bus_config.h"

/* ==================== Pattern 1: Registration Callback (RECOMMENDED) ==================== */

/**
 * Pattern: Library provides registration function, app calls it after event bus init
 *
 * Pros:
 * - Simple and explicit
 * - Clear initialization order
 * - Library doesn't depend on event bus timing
 *
 * Cons:
 * - Application must remember to call registration functions
 */

// ========== Library side (lib/gsm/gsm.h) ==========

typedef struct {
    // GSM configuration
    uint32_t baud_rate;
    // ...
} gsm_config_t;

/**
 * @brief Initialize GSM hardware (does NOT touch event bus)
 *
 * Call this first. This only initializes hardware and creates internal tasks.
 * Does not subscribe to event bus.
 *
 * @param config GSM configuration
 * @return true if successful
 */
bool gsm_init(const gsm_config_t *config);

/**
 * @brief Register GSM event handlers to event bus
 *
 * Call this AFTER event_bus_init_all().
 * This subscribes GSM to receive events it cares about (if any),
 * but usually GSM is publish-only.
 *
 * For publish-only modules, this can be empty or not needed!
 */
void gsm_register_events(void);

// ========== Application side (app/app_main.c) ==========

void app_init_pattern1(void) {
    // Step 1: Initialize event bus
    event_bus_init_all();

    // Step 2: Subscribe application handlers
    SUBSCRIBE_COMM(0, app_comm_handler);
    SUBSCRIBE_SENSOR(0, app_sensor_handler);

    // Step 3: Initialize libraries (hardware init only)
    gsm_config_t gsm_cfg = { .baud_rate = 115200 };
    gsm_init(&gsm_cfg);
    gps_init(NULL);

    // Step 4: Register events (if libraries need to subscribe)
    gsm_register_events();  // Usually empty for publish-only modules
    gps_register_events();  // Usually empty

    // Done! Libraries can now publish, app receives events
}

/* ==================== Pattern 2: Lazy Initialization (SIMPLEST) ==================== */

/**
 * Pattern: Libraries use event_bus_get() which auto-initializes
 *
 * Pros:
 * - Extremely simple
 * - No explicit initialization needed
 * - Works everywhere
 *
 * Cons:
 * - First access is slower (one-time init)
 * - Less control over init timing
 */

// ========== Library side (lib/gsm/gsm_events.c) ==========

void gsm_publish_connected_lazy(void) {
    // event_bus_get() auto-initializes if needed!
    PUBLISH_COMM(EVT_GSM_CONNECTED, NULL, 0);
}

// ========== Application side ==========

void app_init_pattern2(void) {
    // Step 1: Subscribe handlers (event bus auto-inits on first call!)
    SUBSCRIBE_COMM(0, app_comm_handler);
    SUBSCRIBE_SENSOR(0, app_sensor_handler);

    // Step 2: Initialize libraries
    gsm_init(NULL);
    gps_init(NULL);

    // That's it! Event bus is lazily initialized
}

/* ==================== Pattern 3: Init Hook (MOST FLEXIBLE) ==================== */

/**
 * Pattern: Library calls init hook when ready
 *
 * Pros:
 * - Library controls when it's ready
 * - Can do complex setup
 * - Flexible for multi-stage init
 *
 * Cons:
 * - More complex code
 * - Requires function pointers
 */

// ========== Library side (lib/gsm/gsm.h) ==========

typedef void (*gsm_ready_callback_t)(void);

typedef struct {
    uint32_t baud_rate;
    gsm_ready_callback_t on_ready;  // Called when GSM is ready
} gsm_config_init_hook_t;

bool gsm_init_with_hook(const gsm_config_init_hook_t *config);

// ========== Library implementation (lib/gsm/gsm.c) ==========

static gsm_ready_callback_t g_gsm_ready_cb = NULL;

bool gsm_init_with_hook(const gsm_config_init_hook_t *config) {
    g_gsm_ready_cb = config->on_ready;

    // Do hardware init...
    // ...

    return true;
}

static void gsm_task_impl(void *param) {
    // Wait for hardware ready
    while (!gsm_hw_ready()) {
        vTaskDelay(100);
    }

    // Call ready callback
    if (g_gsm_ready_cb) {
        g_gsm_ready_cb();
    }

    // Continue normal operation
    while (1) {
        // ...
    }
}

// ========== Application side ==========

void on_gsm_ready(void) {
    printf("GSM ready! Can now publish events.\n");
    // Do any additional setup
}

void app_init_pattern3(void) {
    // Step 1: Initialize event bus
    event_bus_init_all();

    // Step 2: Subscribe handlers
    SUBSCRIBE_COMM(0, app_comm_handler);

    // Step 3: Initialize library with callback
    gsm_config_init_hook_t cfg = {
        .baud_rate = 115200,
        .on_ready = on_gsm_ready
    };
    gsm_init_with_hook(&cfg);
}

/* ==================== Pattern 4: Two-Phase Init (ESP-IDF STYLE) ==================== */

/**
 * Pattern: Separate init() and start(), subscribe between them
 *
 * Pros:
 * - Clear separation of concerns
 * - Standard pattern (used by ESP-IDF)
 * - Easy to understand
 *
 * Cons:
 * - More API functions
 */

// ========== Library side (lib/gsm/gsm.h) ==========

// Phase 1: Initialize but don't start tasks
bool gsm_init_twophase(const gsm_config_t *config);

// Phase 2: Start operation (create tasks, enable interrupts)
bool gsm_start(void);

// ========== Application side ==========

void app_init_pattern4(void) {
    // Step 1: Initialize event bus
    event_bus_init_all();

    // Step 2: Initialize libraries (no tasks yet)
    gsm_init_twophase(&gsm_cfg);
    gps_init_twophase(&gps_cfg);

    // Step 3: Subscribe event handlers
    SUBSCRIBE_COMM(0, app_comm_handler);
    SUBSCRIBE_SENSOR(0, app_sensor_handler);

    // Step 4: Start libraries (now they can publish!)
    gsm_start();
    gps_start();
}

/* ==================== Recommended Pattern for Your Use Case ==================== */

/**
 * For moving modules/ to lib/:
 *
 * Use Pattern 1 (Registration Callback) or Pattern 2 (Lazy Init)
 *
 * Pattern 1 if:
 * - You want explicit control
 * - You have many libraries
 * - You need clear initialization order
 *
 * Pattern 2 if:
 * - You want simplicity
 * - You trust lazy init
 * - You don't care about first-access penalty
 *
 * Example for lib/gsm:
 */

// ========== lib/gsm/gsm.h ==========
void gsm_init(void);  // Hardware init only

// ========== lib/gsm/gsm_events.c ==========
void gsm_on_connected_callback(void) {
    // Just publish! Auto-init if needed.
    PUBLISH_COMM(EVT_GSM_CONNECTED, NULL, 0);
}

// ========== app/app_main.c ==========
void app_main(void) {
    // Subscribe first (auto-inits event bus)
    SUBSCRIBE_COMM(0, app_comm_handler);

    // Initialize libs
    gsm_init();
    gps_init();

    // Done! Libs publish, app receives.
}

/* ==================== Complete File Organization ==================== */

/**
 * lib/gsm/
 *   ├── gsm.h               - Public API (gsm_init, gsm_send, etc.)
 *   ├── gsm.c               - Core GSM logic
 *   ├── gsm_port.c          - Hardware abstraction (UART, AT)
 *   └── gsm_events.c        - Event publishing ★
 *       Contains:
 *         - gsm_publish_connected()
 *         - gsm_publish_data_rx()
 *         Called from: gsm.c, gsm_port.c callbacks
 *
 * lib/gps/
 *   ├── gps.h
 *   ├── gps.c
 *   ├── gps_port.c
 *   └── gps_events.c        - Event publishing ★
 *
 * app/
 *   ├── app_main.c          - Initialization ★
 *       event_bus_init_all()
 *       Subscribe handlers
 *       Initialize libs
 *
 *   └── app_handlers.c      - Event handlers ★
 *       app_comm_handler()
 *       app_sensor_handler()
 */

/* ==================== Complete Example ==================== */

#if 0
// ========== lib/gsm/gsm.h ==========
typedef struct {
    uint32_t baud_rate;
} gsm_config_t;

void gsm_init(const gsm_config_t *config);
void gsm_send_data(const uint8_t *data, size_t len);

// ========== lib/gsm/gsm_events.c (NEW FILE) ==========
#include "event_bus_config.h"

void gsm_publish_connected(void) {
    PUBLISH_COMM(EVT_GSM_CONNECTED, NULL, 0);
}

void gsm_publish_data_rx(const uint8_t *data, size_t len) {
    PUBLISH_COMM(EVT_GSM_DATA_RX, data, len);
}

// ========== lib/gsm/gsm.c ==========
#include "gsm.h"
#include "gsm_events.c"  // Or compile separately

void gsm_init(const gsm_config_t *config) {
    // Hardware init
    gsm_hw_init(config->baud_rate);
}

void gsm_uart_callback(uint8_t *data, size_t len) {
    // Publish immediately (non-blocking!)
    gsm_publish_data_rx(data, len);
}

// ========== app/app_handlers.c ==========
#include "event_bus_config.h"

void app_comm_handler(const event_msg_t *msg) {
    switch (msg->type) {
        case EVT_GSM_CONNECTED:
            printf("App: GSM connected!\n");
            break;
        case EVT_GSM_DATA_RX:
            printf("App: RX %u bytes\n", msg->size);
            break;
    }
}

// ========== app/app_main.c ==========
#include "event_bus_config.h"
#include "gsm.h"
#include "gps.h"

void app_main(void) {
    // Subscribe (auto-inits event bus)
    SUBSCRIBE_COMM(0, app_comm_handler);
    SUBSCRIBE_SENSOR(0, app_sensor_handler);

    // Initialize libs
    gsm_config_t gsm_cfg = { .baud_rate = 115200 };
    gsm_init(&gsm_cfg);

    gps_init(NULL);

    // Start scheduler
    vTaskStartScheduler();
}
#endif

#endif /* EVENT_BUS_INIT_PATTERNS_H */
