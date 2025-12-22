#include "rs485_app.h"

typedef void (*at_cmd_handler_t)(const char *param);

typedef struct
{
    const char* name;
    at_cmd_handler_t handler;
}at_cmd_entry_t;

void rs485_at_cmd_handler(rs485_instance_t *inst);