#include "ble_app.h"

typedef void (*ble_at_cmd_handler_t)(ble_instance_t *inst, const char *param);

typedef struct
{
    const char* name;
    ble_at_cmd_handler_t handler;
}ble_at_cmd_entry_t;

void ble_at_cmd_handler(ble_instance_t *inst);