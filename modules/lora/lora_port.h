#ifndef LORA_PORT_H
#define LORA_PORT_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "lora.h"
#include "board_config.h"

int lora_port_init_instance(lora_t *lora_handle);
void lora_port_start(lora_t *lora_handle);
void lora_port_stop(lora_t *lora_handle);
uint32_t lora_port_get_rx_pos();
char *lora_port_get_recv_buf();
void lora_port_set_queue(QueueHandle_t queue);



int lora_uart3_hw_init(void);
int lora_uart3_comm_start(void);
int lora_uart3_send(const char *data, size_t len);

#endif
