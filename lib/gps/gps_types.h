#ifndef GPS_TYPES_H
#define GPS_TYPES_H

#include "gps_config.h"
#include <stdint.h>

/**
 * @brief 처리중인 GPS 프로토콜 종류
 *
 */
typedef enum {
  GPS_PROTOCOL_NONE = 0,
  GPS_PROTOCOL_NMEA = 1,
  GPS_PROTOCOL_UBX = 2,
  GPS_PROTOCOL_UNICORE_BIN = 3,
  GPS_PROTOCOL_UNICORE = 4,
  GPS_PROTOCOL_RTCM = 5,
  GPS_PROTOCOL_INVALID = UINT8_MAX
} gps_procotol_t;

typedef enum {
  GPS_EVENT_NONE = 0,

  /* 공통 데이터 이벤트 */
  GPS_EVENT_DATA_PARSED, // 프로토콜 데이터 파싱 완료

  GPS_EVENT_INVALID = UINT8_MAX
} gps_event_t;

/**
 * @brief GPS 파싱 상태
 *
 */
typedef enum {
  GPS_PARSE_STATE_NONE = 0,

  /* NMEA 183 protocol */
  GPS_PARSE_STATE_NMEA_START = 1,
  GPS_PARSE_STATE_NMEA_ADDR = 2,
  GPS_PARSE_STATE_NMEA_DATA = 3,
  GPS_PARSE_STATE_NMEA_CHKSUM = 4,
  GPS_PARSE_STATE_NMEA_END_SEQ = 5,

  /* UBX protocol */
  GPS_PARSE_STATE_UBX_SYNC_1 = 10,
  GPS_PARSE_STATE_UBX_SYNC_2 = 11,
  GPS_PARSE_STATE_UBX_MSG_CLASS = 12,
  GPS_PARSE_STATE_UBX_MSG_ID = 13,
  GPS_PARSE_STATE_UBX_LEN = 14,
  GPS_PARSE_STATE_UBX_PAYLOAD = 15,
  GPS_PARSE_STATE_UBX_CHKSUM_A = 16,
  GPS_PARSE_STATE_UBX_CHKSUM_B = 17,

  /* UNICORE protocol */
  GPS_PARSE_STATE_UNICORE_START = 20,
  GPS_PARSE_STATE_UNICORE_DATA = 21,
  GPS_PARSE_STATE_UNICORE_CHKSUM = 22,

  /* UNICORE BINARY protocol */
  GPS_PARSE_STATE_UNICORE_SYNC1 = 30,
  GPS_PARSE_STATE_UNICORE_SYNC2 = 31,
  GPS_PARSE_STATE_UNICORE_SYNC3 = 32,
  GPS_PARSE_STATE_UNICORE_MESSAGE_ID = 33,
  GPS_PARSE_STATE_UNICORE_MESSAGE_LEN = 34,
  GPS_PARSE_STATE_UNICORE_PAYLOAD = 35,
  GPS_PARSE_STATE_UNICORE_CRC = 36,

  /* RTCM3 protocol */
  GPS_PARSE_STATE_RTCM_PREAMBLE = 40,
  GPS_PARSE_STATE_RTCM_LEN_1 = 41,
  GPS_PARSE_STATE_RTCM_LEN_2 = 42,
  GPS_PARSE_STATE_RTCM_PAYLOAD = 43,
  GPS_PARSE_STATE_RTCM_CRC = 44,
  
  GPS_PARSE_STATE_INVALID = UINT8_MAX
} gps_parse_state_t;

/**
 * @brief NMEA183 프로토콜 sentence
 *
 */
typedef enum {
  GPS_NMEA_MSG_NONE = 0,
  GPS_NMEA_MSG_GGA = 1,
  GPS_NMEA_MSG_RMC = 2,
  GPS_NMEA_MSG_THS = 3,
  GPS_NMEA_MSG_INVALID = UINT8_MAX
} gps_nmea_msg_t;

typedef union {
  gps_nmea_msg_t nmea;
  struct {
    uint8_t class;
    uint8_t id;
  } ubx;
  struct {
    uint16_t msg;
  } unicore_bin;
  struct {
    uint8_t response;
  } unicore;
  struct {
    uint16_t msg_type;
  } rtcm;
} gps_msg_t;

#endif
