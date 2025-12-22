#include "gps.h"
#include "gps_config.h"
#include "parser.h"
#include <string.h>

#ifndef TAG
  #define TAG "GPS_PARSE"
#endif

#include "log.h"

static inline void add_nmea_chksum(gps_t *gps, char ch);
static inline uint8_t check_nmea_chksum(gps_t *gps);
static inline void term_add(gps_t *gps, char ch);
static inline void term_next(gps_t *gps);

void _gps_gga_raw_add(gps_t *gps, char ch) {
  if (gps->nmea_data.gga_raw_pos < 99) {
    gps->nmea_data.gga_raw[gps->nmea_data.gga_raw_pos] = ch;
    gps->nmea_data.gga_raw[++gps->nmea_data.gga_raw_pos] = '\0';
  }
}


/**
 * @brief NMEA 프로토콜 체크섬 추가
 *
 * @param[out] gps
 * @param[in] ch
 */
static inline void add_nmea_chksum(gps_t *gps, char ch) {
  gps->nmea.crc ^= (uint8_t)ch;
}

/**
 * @brief NMEA 프로토콜 체크섬 확인
 *
 * @param[in] gps
 * @return uint8_t 1: success 0: fail
 */
static inline uint8_t check_nmea_chksum(gps_t *gps) {
  uint8_t crc = 0;

  crc = (uint8_t)((((PARSER_CHAR_HEX_TO_NUM(gps->nmea.term_str[0])) & 0x0FU)
                   << 0x04U) |
                  ((PARSER_CHAR_HEX_TO_NUM(gps->nmea.term_str[1])) & 0x0FU));

  if (gps->nmea.crc != crc) {
    return 0;
  }

  return 1;
}

/**
 * @brief 프로토콜 페이로드 추가
 *
 * @param[inout] gps
 * @param[in] ch
 */
static inline void add_payload(gps_t *gps, char ch) {
  if (gps->pos < GPS_PAYLOAD_SIZE - 1) {
    gps->payload[gps->pos] = ch;
    gps->payload[++gps->pos] = 0;
  }
}

/**
 * @brief NMEA183 프로토콜 , 사이에 있는 문자 추가
 *
 * @param[inout] gps
 * @param[in] ch
 */
static inline void term_add(gps_t *gps, char ch) {
  if (gps->nmea.term_pos < GPS_NMEA_TERM_SIZE - 1) {
    gps->nmea.term_str[gps->nmea.term_pos] = ch;
    gps->nmea.term_str[++gps->nmea.term_pos] = 0;
  }
}

/**
 * @brief NMEA183 프로토콜 , 파싱후 초기화
 *
 * @param[out] gps
 */
static inline void term_next(gps_t *gps) {
  gps->nmea.term_str[0] = 0;
  gps->nmea.term_pos = 0;
  gps->nmea.term_num++;
}

static inline void term_add_unicore(gps_t *gps, char ch) {
  if (gps->unicore.term_pos < GPS_UNICORE_TERM_SIZE - 1) {
    gps->unicore.term_str[gps->unicore.term_pos] = ch;
    gps->unicore.term_str[++gps->unicore.term_pos] = 0;
  }
}

/**
 * @brief UNICORE ASCII 프로토콜 , 파싱후 초기화
 *
 * @param[out] gps
 */
static inline void term_next_unicore(gps_t *gps) {
  gps->unicore.term_str[0] = 0;
  gps->unicore.term_pos = 0;
  gps->unicore.term_num++;
}

static inline void add_unicore_chksum(gps_t *gps, char ch) {
  gps->unicore.crc ^= (uint8_t)ch;
}

static inline uint8_t check_unicore_chksum(gps_t *gps) {
  uint8_t crc = 0;
  crc = (uint8_t)((((PARSER_CHAR_HEX_TO_NUM(gps->unicore.term_str[0])) & 0x0FU)
                   << 0x04U) |
                  ((PARSER_CHAR_HEX_TO_NUM(gps->unicore.term_str[1])) & 0x0FU));

  if (gps->unicore.crc != crc) {
    return 0;
  }

  return 1;
}

/**
 * @brief gps 객체 초기화
 *
 * @param[out] gps
 */
void gps_init(gps_t *gps) {
  memset(gps, 0, sizeof(*gps));
  gps->mutex = xSemaphoreCreateMutex();
  ubx_cmd_handler_init(&gps->ubx_cmd_handler);
  ubx_init_context_init(&gps->ubx_init_ctx);
}

/**
 * @brief GPS 프로토콜 파싱
 *
 * @param[inout] gps
 * @param[in] data
 * @param[in] len
 */
void gps_parse_process(gps_t *gps, const void *data, size_t len) {
  const uint8_t *d = data;

  for (; len > 0; ++d, --len) {
    /* @TODO GPS_PROTOCOL_NONE 일때 start 문자 찾는걸 만들고, 프로토콜에 따라
     * 다르게 파싱하게끔 만들기 */
    if (gps->protocol == GPS_PROTOCOL_NONE) {
      if (*d == '$') {
        memset(&gps->nmea, 0, sizeof(gps->nmea));

        gps->protocol = GPS_PROTOCOL_NMEA;
        gps->state = GPS_PARSE_STATE_NMEA_START;
      } 
      /* UBX binary */
      else if (*d == 0xB5 && gps->state == GPS_PARSE_STATE_NONE) {
        gps->state = GPS_PARSE_STATE_UBX_SYNC_1;
      } 
      else if (*d == 0x62 && gps->state == GPS_PARSE_STATE_UBX_SYNC_1) {
        memset(&gps->ubx, 0, sizeof(gps->ubx));
        gps->pos = 0;
        gps->protocol = GPS_PROTOCOL_UBX;
        gps->state = GPS_PARSE_STATE_UBX_SYNC_2;
      } 
      /* UNICORE binary */
      else if(*d == 0xAA && gps->state == GPS_PARSE_STATE_NONE) {
        gps->state = GPS_PARSE_STATE_UNICORE_SYNC1;
        gps->pos = 0;
        add_payload(gps, *d);
      } else if(*d == 0x44 && gps->state == GPS_PARSE_STATE_UNICORE_SYNC1) {
        gps->state = GPS_PARSE_STATE_UNICORE_SYNC2;
        add_payload(gps, *d);
      } else if(*d == 0xB5 && gps->state == GPS_PARSE_STATE_UNICORE_SYNC2) {
        memset(&gps->unicore_bin, 0, sizeof(gps->unicore_bin));
        add_payload(gps, *d);

        gps->protocol = GPS_PROTOCOL_UNICORE_BIN;
        gps->state = GPS_PARSE_STATE_UNICORE_SYNC3;
      }
      /* RTCM3 */
      else if(*d == 0xD3 && gps->state == GPS_PARSE_STATE_NONE) {
        memset(&gps->rtcm, 0, sizeof(gps->rtcm));
        gps->pos = 0;
        add_payload(gps, *d);
        gps->protocol = GPS_PROTOCOL_RTCM;
        gps->state = GPS_PARSE_STATE_RTCM_PREAMBLE;
      }
      /* 재시도 로직 */
      else {
        gps->state = GPS_PARSE_STATE_NONE;

        if (*d == '$') {
          memset(&gps->nmea, 0, sizeof(gps->nmea));
          gps->protocol = GPS_PROTOCOL_NMEA;
          gps->state = GPS_PARSE_STATE_NMEA_START;
        } else if (*d == 0xD3) {
          memset(&gps->rtcm, 0, sizeof(gps->rtcm));
          gps->pos = 0;
          add_payload(gps, *d);
          gps->protocol = GPS_PROTOCOL_RTCM;
          gps->state = GPS_PARSE_STATE_RTCM_PREAMBLE;
        }else if (*d == 0xB5) {
          gps->state = GPS_PARSE_STATE_UBX_SYNC_1;
        } else if (*d == 0xAA) {
          gps->state = GPS_PARSE_STATE_UNICORE_SYNC1;
          gps->pos = 0;
          add_payload(gps, *d);
        }
      }
    } 
    else if (gps->protocol == GPS_PROTOCOL_NMEA) {
#if defined(USE_STORE_RAW_GGA)
      if (gps->nmea.msg_type == GPS_NMEA_MSG_GGA) {
        _gps_gga_raw_add(gps, *d);
      }
#endif

      if (*d == ',') {
            if (gps->nmea.term_num == 0 && strcmp(gps->nmea.term_str, "command") == 0) {

            	memset(&gps->unicore, 0, sizeof(gps->unicore));
            	gps->unicore.crc = gps->nmea.crc;
            	memset(&gps->nmea, 0, sizeof(gps->nmea));
              gps->protocol = GPS_PROTOCOL_UNICORE;
              gps->state = GPS_PARSE_STATE_UNICORE_START;
              gps->unicore.msg_type = GPS_UNICORE_MSG_COMMAND;
              add_unicore_chksum(gps, *d);
              term_next_unicore(gps);
            } else {
              gps_parse_nmea_term(gps);
              add_nmea_chksum(gps, *d);
              term_next(gps);
            }
      }
      else if (*d == '*') {
        gps_parse_nmea_term(gps);
        gps->nmea.star = 1;
        term_next(gps);

        gps->state = GPS_PARSE_STATE_NMEA_CHKSUM;
      } else if (*d == '\r') {
        if (check_nmea_chksum(gps)) {

#if defined(USE_STORE_RAW_GGA)
          if (gps->nmea.msg_type == GPS_NMEA_MSG_GGA) {
            _gps_gga_raw_add(gps, *d);
            _gps_gga_raw_add(gps, '\n');
            gps->nmea_data.gga_is_rdy = true;
          }
#endif
          gps_msg_t msg;
          msg.nmea = gps->nmea.msg_type;
          
          if (gps->handler) {
            gps->handler(gps, GPS_EVENT_DATA_PARSED, GPS_PROTOCOL_NMEA, msg);
          }
        }

        gps->pos = 0;
        gps->protocol = GPS_PROTOCOL_NONE;
        gps->state = GPS_PARSE_STATE_NONE;
      } else {
        if (!gps->nmea.star) {
          add_nmea_chksum(gps, *d);
        }
        term_add(gps, *d);
      }
    } else if (gps->protocol == GPS_PROTOCOL_UBX) {
      add_payload(gps, *d);
      gps_parse_ubx(gps);
    } 
    else if(gps->protocol == GPS_PROTOCOL_UNICORE)
    {
      if (*d == ',') {
        gps_parse_unicore_term(gps);
        
        if (!gps->unicore.colon) {
          add_unicore_chksum(gps, *d);
        }
        term_next_unicore(gps);
      }
      else if (*d == ':') {
        // ':' 이후는 값이므로 CRC에 포함하지 않음
        gps->unicore.colon = 1;
        add_unicore_chksum(gps, *d);  // ':' 자체는 CRC에 포함
        term_add_unicore(gps, *d);
      }
      else if (*d == '*') {
        gps_parse_unicore_term(gps);
        gps->unicore.star = 1;
        term_next_unicore(gps);
        gps->state = GPS_PARSE_STATE_UNICORE_CHKSUM;
      } else if (*d == '\r') {
        if (check_unicore_chksum(gps)) {
          gps_msg_t msg;
          msg.unicore.response = gps->unicore.response;

          if (gps->handler) {
            gps->handler(gps, GPS_EVENT_DATA_PARSED, gps->protocol, msg);
          }
        }

        memset(&gps->unicore, 0, sizeof(gps->unicore));
        memset(gps->payload, 0, sizeof(gps->payload));
        gps->protocol = GPS_PROTOCOL_NONE;
        gps->state = GPS_PARSE_STATE_NONE;
      } else {
        if (!gps->unicore.star && !gps->unicore.colon) {
          add_unicore_chksum(gps, *d);
        }

        term_add_unicore(gps, *d);
      }
    }
    else if( gps->protocol == GPS_PROTOCOL_UNICORE_BIN)
    {
      add_payload(gps, *d);
      gps_parse_unicore_bin(gps);
    }
    else if (gps->protocol == GPS_PROTOCOL_RTCM) {
      add_payload(gps, *d);
      if (gps->state == GPS_PARSE_STATE_RTCM_PREAMBLE) {
    	gps->rtcm.msg_len = (*d & 0x03) << 8;
        gps->state = GPS_PARSE_STATE_RTCM_LEN_1;
      }
      else if (gps->state == GPS_PARSE_STATE_RTCM_LEN_1) {
        gps->rtcm.msg_len |= *d;
        gps->rtcm.total_len = 3 + gps->rtcm.msg_len + 3;  // 헤더(3) + 페이로드 + CRC(3)
        gps->rtcm.payload_cnt = 0;
        gps->state = GPS_PARSE_STATE_RTCM_PAYLOAD;
      }
      else if (gps->state == GPS_PARSE_STATE_RTCM_PAYLOAD) {
        gps->rtcm.payload_cnt++;
        if (gps->rtcm.payload_cnt == 1) {
          gps->rtcm.msg_type = (*d) << 4;
        }
        else if (gps->rtcm.payload_cnt == 2) {
          gps->rtcm.msg_type |= ((*d) >> 4) & 0x0F;
        }

        if (gps->pos >= gps->rtcm.total_len) {
          gps_msg_t msg;
          msg.rtcm.msg_type = gps->rtcm.msg_type;
          if (gps->handler) {
            gps->handler(gps, GPS_EVENT_DATA_PARSED, GPS_PROTOCOL_RTCM, msg);
          }

          memset(&gps->rtcm, 0, sizeof(gps->rtcm));
          memset(gps->payload, 0, sizeof(gps->payload));
          gps->protocol = GPS_PROTOCOL_NONE;
          gps->state = GPS_PARSE_STATE_NONE;
        }
      }
    }
  }
}

void gps_set_evt_handler(gps_t *gps, evt_handler handler) {
  if (handler) {
    gps->handler = handler;
  }
}
