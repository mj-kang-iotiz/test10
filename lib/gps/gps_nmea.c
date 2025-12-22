#include "gps_nmea.h"
#include "gps.h"
#include "gps_parse.h"
#include <string.h>

static double parse_lat_lon(gps_t *gps);
static void parse_nmea_gga(gps_t *gps);

/**
 * @brief 십진수 도 변환
 *
 * @param[in] gps
 * @return double
 */
static double parse_lat_lon(gps_t *gps) {
  double val, deg, min;

  val = gps_parse_double(gps);
  deg = (double)((int)(((int)val / 100)));
  min = val - (deg * (double)100);
  val = deg + (min / (double)(60));

  return val;
}

/**
 * @brief GGA 파싱
 *
 * @param[inout] gps
 */
static void parse_nmea_gga(gps_t *gps) {
  switch (gps->nmea.term_num) {
  case 1: // time
    if (gps->nmea.term_pos >= 6) {
      gps->nmea_data.gga.hour =
          PARSER_CHAR_DEC_TO_NUM(gps->nmea.term_str[0]) * 10 +
          PARSER_CHAR_DEC_TO_NUM(gps->nmea.term_str[1]);
      gps->nmea_data.gga.min =
          PARSER_CHAR_DEC_TO_NUM(gps->nmea.term_str[2]) * 10 +
          PARSER_CHAR_DEC_TO_NUM(gps->nmea.term_str[3]);
      gps->nmea_data.gga.sec =
          PARSER_CHAR_DEC_TO_NUM(gps->nmea.term_str[4]) * 10 +
          PARSER_CHAR_DEC_TO_NUM(gps->nmea.term_str[5]);
    }
    break;

  case 2: // lat
    gps->nmea_data.gga.lat = parse_lat_lon(gps);
    break;

  case 3: // NS
    // S면 위도 음수
    gps->nmea_data.gga.ns = gps->nmea.term_str[0];
    break;

  case 4: // lon
    gps->nmea_data.gga.lon = parse_lat_lon(gps);
    break;

  case 5: // EW
    // W면 경도 음수
    gps->nmea_data.gga.ew = gps->nmea.term_str[0];
    break;

  case 6: // quality(fix)
    gps->nmea_data.gga.fix = gps_parse_number(gps);
    break;

  case 7: // sat num
    gps->nmea_data.gga.sat_num = gps_parse_number(gps);
    break;

  case 8: // HDOP
    gps->nmea_data.gga.hdop = gps_parse_float(gps);
    break;

  case 9: // alt
    gps->nmea_data.gga.alt = gps_parse_float(gps);
    break;

    // case 10 : // alt unit 'M'

  case 11: // geoid separation
    gps->nmea_data.gga.geo_sep = gps_parse_float(gps);
    break;

    // case 12: // sep unit 'M'
    // case 13: // diffAge
    // case 14: // diffStation

  default:
    break;
  }
}

static void parse_nmea_gpths(gps_t *gps)
{
  switch (gps->nmea.term_num) {
  case 1:
    gps->nmea_data.ths.heading = gps_parse_double(gps);
    break;

  case 2:
    gps->nmea_data.ths.mode = gps_parse_character(gps);
    break;

  default:
    break;
  }
}

/**
 * @brief NMEA183 프로토콜 파싱
 *
 * @param[inout]  gps
 * @return uint8_t 1: success, 0: fail
 */
uint8_t gps_parse_nmea_term(gps_t *gps) {
  if (gps->nmea.msg_type == GPS_NMEA_MSG_NONE) {
    if (gps->state == GPS_PARSE_STATE_NMEA_START) {
      const char *talker = gps->nmea.term_str;
      const char *msg = &gps->nmea.term_str[2];

      if (!strncmp(msg, "GGA", 3)) {
          gps->nmea.msg_type = GPS_NMEA_MSG_GGA;

#if defined(USE_STORE_RAW_GGA)
          gps->nmea_data.gga_raw_pos = 0;
          _gps_gga_raw_add(gps, '$');
          for (int i = 0; i < 5; i++) {
            _gps_gga_raw_add(gps, gps->nmea.term_str[i]);
          }
          _gps_gga_raw_add(gps, ',');
          gps->nmea_data.gga_is_rdy = false;
#endif

      } else if (!strncmp(msg, "RMC", 3)) {
        gps->nmea.msg_type = GPS_NMEA_MSG_RMC;
      } else if (!strncmp(msg, "THS", 3)) {
        gps->nmea.msg_type = GPS_NMEA_MSG_THS;
      }
      else {
        gps->nmea.msg_type = GPS_NMEA_MSG_NONE;
        gps->state = GPS_PARSE_STATE_NONE;

        return 0;
      }

      gps->state = GPS_PARSE_STATE_NMEA_DATA;
    }

    return 1;
  }

  if (gps->nmea.msg_type == GPS_NMEA_MSG_GGA) {
    parse_nmea_gga(gps);
  } else if (gps->nmea.msg_type == GPS_NMEA_MSG_THS) {
	  parse_nmea_gpths(gps);
  }

  return 1;
}
