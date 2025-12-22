#ifndef GPS_PORT_H
#define GPS_PORT_H

#include "FreeRTOS.h"
#include "gps_app.h"
#include "queue.h"
#include "task.h"

int gps_port_init_instance(gps_t *gps_handle, gps_id_t id, gps_type_t type);
void gps_port_start(gps_t *gps_handle);
void gps_port_stop(gps_t *gps_handle);
uint32_t gps_port_get_rx_pos(gps_id_t id);
char *gps_port_get_recv_buf(gps_id_t id);
void gps_port_set_queue(gps_id_t id, QueueHandle_t queue);
void gps_port_cleanup_instance(gps_id_t id);


#endif
