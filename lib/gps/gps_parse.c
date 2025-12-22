#include "gps_parse.h"
#include "gps.h"

/**
 * @brief GPS 프로토콜 숫자 파싱
 *
 * @param[in] gps
 * @return int32_t 숫자
 */
int32_t gps_parse_number(gps_t *gps) {
  char *term = gps->nmea.term_str;
  uint8_t minus = 0;
  int32_t res = 0;

  for (; term != NULL && *term == ' '; ++term) {
  }

  minus = (*term == '-' ? (++term, 1) : 0);
  for (; term != NULL && PARSER_CHAR_IS_NUM(*term); ++term) {
    res = 10L * res + PARSER_CHAR_DEC_TO_NUM(*term);
  }

  return minus ? -res : res;
}

/**
 * @brief GPS 프로토콜 부동소수점 double 파싱
 *
 * @param[in] gps
 * @return double
 */
double gps_parse_double(gps_t *gps) {
  double val = 0;
  double power = 1;
  double sign = 1;

  char *term = gps->nmea.term_str;

  for (; term != NULL && *term == ' '; ++term) {
  }

  if (*term == '-') {
    sign = -1;
    ++term;
  }
  while (PARSER_CHAR_IS_NUM(*term)) {
    val = val * (double)10 + PARSER_CHAR_DEC_TO_NUM(*term);
    ++term;
  }
  if (*term == '.') {
    ++term;
  }
  while (PARSER_CHAR_IS_NUM(*term)) {
    val = val * (double)10 + PARSER_CHAR_DEC_TO_NUM(*term);
    power *= (double)10;
    ++term;
  }

  return sign * val / power;
}

/**
 * @brief GPS 프로토콜 부동소수점 float 파싱
 *
 * @param[in] gps
 * @return float
 */
float gps_parse_float(gps_t *gps) {
  float val = 0;
  float power = 1;
  float sign = 1;

  char *term = gps->nmea.term_str;

  for (; term != NULL && *term == ' '; ++term) {
  }

  if (*term == '-') {
    sign = -1;
    ++term;
  }
  while (PARSER_CHAR_IS_NUM(*term)) {
    val = val * (float)10 + PARSER_CHAR_DEC_TO_NUM(*term);
    ++term;
  }
  if (*term == '.') {
    ++term;
  }
  while (PARSER_CHAR_IS_NUM(*term)) {
    val = val * (float)10 + PARSER_CHAR_DEC_TO_NUM(*term);
    power *= (float)10;
    ++term;
  }

  return sign * val / power;
}

/**
 * @brief GPS 프로토콜 문자 파싱
 *
 * @param[in] gps
 * @return char
 */
char gps_parse_character(gps_t *gps) {
  char *term = gps->nmea.term_str;

  for (; term != NULL && *term == ' '; ++term) {
  }

  return *term;
}