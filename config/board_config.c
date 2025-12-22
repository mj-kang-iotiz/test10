#include "board_config.h"

static const board_config_t current_config = {
    .board = BOARD_TYPE,
    .gps =
        {
            [GPS_ID_BASE] = GPS1_TYPE,
            [GPS_ID_ROVER] = GPS2_TYPE,
        },
    .gps_cnt = GPS_CNT,
    .lora_mode = LORA_MODE,
    .use_ble = (USE_BLE ? 1 : 0),
    .use_rs485 = (USE_RS485 ? 1 : 0),
    .use_gsm = (USE_GSM ? 1 : 0)};

const board_config_t *board_get_config(void) { return &current_config; }