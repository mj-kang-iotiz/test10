#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "board_type.h"
#include <stdbool.h>
#include <stdint.h>

#if defined(BOARD_TYPE_BASE_UNICORE)
#define BOARD_TYPE BOARD_TYPE_BASE_UM982
#define GPS1_TYPE GPS_TYPE_UM982
#define GPS2_TYPE GPS_TYPE_NONE
#define GPS_CNT 1
#define LORA_MODE LORA_MODE_BASE
#define USE_BLE 1
#define USE_RS485 0
#define USE_GSM 1
#elif defined(BOARD_TYPE_BASE_UBLOX)
#define BOARD_TYPE BOARD_TYPE_BASE_F9P
#define GPS1_TYPE GPS_TYPE_F9P
#define GPS2_TYPE GPS_TYPE_NONE
#define GPS_CNT 1
#define LORA_MODE LORA_MODE_BASE
#define USE_BLE 1
#define USE_RS485 0
#define USE_GSM 1
#elif defined(BOARD_TYPE_ROVER_UNICORE)
#define BOARD_TYPE BOARD_TYPE_ROVER_UM982
#define GPS1_TYPE GPS_TYPE_UM982
#define GPS2_TYPE GPS_TYPE_NONE
#define GPS_CNT 1
#define LORA_MODE LORA_MODE_ROVER
#define USE_BLE 0
#define USE_RS485 1
#define USE_GSM 1
#define USE_SOFTUART 0
#elif defined(BOARD_TYPE_ROVER_UBLOX)
#define BOARD_TYPE BOARD_TYPE_ROVER_F9P
#define GPS1_TYPE GPS_TYPE_F9P ///< base
#define GPS2_TYPE GPS_TYPE_F9P ///< rover
#define GPS_CNT 2
#define LORA_MODE LORA_MODE_ROVER
#define USE_BLE 0
#define USE_RS485 1
#define USE_GSM 1
#define USE_SOFTUART 0
#else
#define BOARD_TYPE BOARD_TYPE_NONE
#define GPS1_TYPE GPS_TYPE_NONE
#define GPS2_TYPE GPS_TYPE_NONE
#define GPS_CNT 0
#define LORA_MODE LORA_MODE_NONE
#define USE_BLE 0
#define USE_RS485 0
#define USE_GSM 0
#endif

typedef enum {
  BOARD_TYPE_NONE = 0,
  BOARD_TYPE_BASE_UM982,
  BOARD_TYPE_BASE_F9P,
  BOARD_TYPE_ROVER_UM982,
  BOARD_TYPE_ROVER_F9P
} board_type_t;

typedef enum {
  GPS_TYPE_NONE = 0,
  GPS_TYPE_F9P,
  GPS_TYPE_UM982,
} gps_type_t;

typedef enum {
  GPS_ID_BASE = 0,
  GPS_ID_ROVER,
  GPS_ID_MAX
} gps_id_t;

typedef enum {
  LORA_MODE_NONE = 0,
  LORA_MODE_BASE,
  LORA_MODE_ROVER
} lora_mode_t;

typedef struct {
  board_type_t board;
  gps_type_t gps[GPS_ID_MAX];
  uint8_t gps_cnt;
  lora_mode_t lora_mode;
  bool use_ble;
  bool use_rs485;
  bool use_gsm;
} board_config_t;

const board_config_t *board_get_config(void);

#endif
