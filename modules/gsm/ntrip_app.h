#ifndef NTRIP_APP_H
#define NTRIP_APP_H

#include "gsm.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief NTRIP TCP 수신 태스크 생성
 *
 * @param gsm GSM 핸들
 */
void ntrip_task_create(gsm_t *gsm);
int ntrip_send_gga_data(const char *data, uint8_t len);
bool ntrip_gga_send_queue_initialized(void);
void ntrip_stop(void);
bool ntrip_is_connected(void);

#endif // NTRIP_TASK_H