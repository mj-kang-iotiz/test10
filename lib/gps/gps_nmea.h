#ifndef GPS_NMEA_H
#define GPS_NMEA_H

#include "gps_types.h"
#include <stdbool.h>
#include <stdint.h>

#define GPS_NMEA_TERM_SIZE 20

/**
 * @brief GGA quality fix 상태
 *
 */
typedef enum {
  GPS_FIX_INVALID = 0,
  GPS_FIX_GPS = 1,
  GPS_FIX_DGPS = 2,
  GPS_FIX_PPS = 3,
  GPS_FIX_RTK_FIX = 4,
  GPS_FIX_RTK_FLOAT = 5,
  GPS_FIX_DR = 6,
  GPS_FIX_MANUAL_POS = 7,
  GPS_FIX_SIMULATOR = 8
} gps_fix_t;

/**
 * @brief GGA 데이터
 *
 * @note 패킷 예시
 * $xxGGA,time,lat,NS,lon,EW,quality,numSV,HDOP,alt,altUnit,sep,sepUnit,diffAge,diffStation*cs\r\n
 * @note 패킷 예시
 * $GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B\r\n
 */
typedef struct {
  uint8_t hour;
  uint8_t min;
  uint8_t sec;
  double lat;
  char ns;
  double lon;
  char ew;
  gps_fix_t fix;
  uint8_t sat_num;
  double hdop;
  double alt;
  double geo_sep;
} gps_gga_t;

typedef enum
{
  GPS_THS_MODE_AUTO = 'A',
  GPS_THS_MODE_DR = 'E',
  GPS_THS_MODE_MANUAL = 'M',
  GPS_THS_MODE_SIMULATOR = 'S',
  GPS_THS_MODE_INVALID = 'V'
}gps_ths_mode_t;

typedef struct
{
  double heading;
  gps_ths_mode_t mode;
}gps_ths_t;

/**
 * @brief 파싱한 NMEA 183 프로토콜 데이터
 *
 */
typedef struct {
  gps_gga_t gga;
#if defined(USE_STORE_RAW_GGA)
  char gga_raw[120];
  uint8_t gga_raw_pos;
  bool gga_is_rdy;
#endif
  gps_ths_t ths;
} gps_nmea_data_t;

/**
 * @brief NMEA 파싱에 필요한 변수
 *
 */
typedef struct {
  char term_str[GPS_NMEA_TERM_SIZE];
  uint8_t term_pos;
  uint8_t term_num;

  gps_nmea_msg_t msg_type;
  uint8_t crc;
  uint8_t star;
} gps_nmea_parser_t;

typedef struct gps_s gps_t;

uint8_t gps_parse_nmea_term(gps_t *gps);

#endif
