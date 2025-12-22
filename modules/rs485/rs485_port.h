#ifndef RS485_PORT_H
#define RS485_PORT_H

#include <stdbool.h>
#include <stdint.h>
#include "rs485.h"
#include "FreeRTOS.h"
#include "queue.h"

int rs485_port_init_instance(rs485_t *rs485_handle);
void rs485_port_start(rs485_t *rs485_handle);
void rs485_port_stop(rs485_t *rs485_handle);

uint32_t rs485_port_get_rx_pos(void);
char *rs485_port_get_recv_buf(void);

void rs485_port_set_queue(QueueHandle_t queue);

#endif