#ifndef BLE_PORT_H
#define BLE_PORT_H

#include <stdbool.h>
#include <stdint.h>
#include "ble.h"
#include "FreeRTOS.h"
#include "queue.h"

int ble_port_init_instance(ble_t *ble_handle);
void ble_port_start(ble_t *ble_handle);
void ble_port_stop(ble_t *ble_handle);

uint32_t ble_port_get_rx_pos(void);
char *ble_port_get_recv_buf(void);

void ble_port_set_queue(QueueHandle_t queue);
int ble_uart5_recv_line_poll(char *buf, size_t buf_size, uint32_t timeout_ms);
#endif
