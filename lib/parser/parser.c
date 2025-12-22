#include "parser.h"

/**
 * @brief
 *
 * @param str
 * @return int32_t
 *
 * @warning 리턴 값 오버플로우 검사가 없음
 * @warning GSM 처리할때 에외 문자 추가 필요
 */
// $GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B\r\n
int32_t parse_int32(const char **str) {
  int32_t val = 0;
  uint8_t minus = 0;

  const char *p = *str;

  if (*p == ',') {
    ++p;
  }
  if (*p == '-') {
    minus = 1;
    ++p;
  }

  while (PARSER_CHAR_IS_NUM(*p)) {
    val = val * 10 + PARSER_CHAR_DEC_TO_NUM(*p);
    ++p;
  }
  *str = p;

  return minus ? -val : val;
}

uint32_t parse_uint32(const char **str) {
  uint32_t val = 0;

  const char *p = *str;

  if (*p == ',') {
    ++p;
  }

  while (PARSER_CHAR_IS_NUM(*p)) {
    val = val * 10 + PARSER_CHAR_DEC_TO_NUM(*p);
    ++p;
  }
  *str = p;

  return val;
}

uint32_t parse_hex(const char **str) {
  int32_t val = 0;
  const char *p = *str;

  if (*p == ',') {
    ++p;
  }

  while (PARSER_CHAR_IS_HEX(*p)) {
    val = val * 16 + PARSER_CHAR_HEX_TO_NUM(*p);
    ++p;
  }
  *str = p;

  return val;
}

double parse_double(const char **str) {
  double val = 0;
  double power = 1;
  double sign = 1;

  const char *p = *str;

  if (*p == ',') {
    ++p;
  }

  if (*p == '-') {
    sign = -1;
    ++p;
  }

  while (PARSER_CHAR_IS_NUM(*p)) {
    val = val * (double)10 + PARSER_CHAR_DEC_TO_NUM(*p);
    ++p;
  }
  if (*p == '.') {
    ++p;
  }
  while (PARSER_CHAR_IS_NUM(*p)) {
    val = val * (double)10 + PARSER_CHAR_DEC_TO_NUM(*p);
    power *= (double)10;
    ++p;
  }
  *str = p;

  return sign * val / power;
}

char parse_char(const char **str) {
  char ch = 0;
  const char *p = *str;

  if (*p == ',') {
    ++p;
  }

  if (PARSER_IS_CHAR(*p)) {
    ch = *p;
    ++p;
  }
  *str = p;

  return ch;
}

uint8_t parse_string(const char **src, char *dst, size_t len) {
  const char *p = *src;
  size_t i = 0;

  if (!dst) {
    return 0;
  }

  if (*p == '$') {
    ++p;
  }
  if (*p == ',') {
    ++p;
  }
  if (*p == '\r') {
    ++p;
  }
  if (*p == '\n') {
    ++p;
  }

  // 종료 문자 길이 확보
  if (len > 0) {
    --len;
  }

  while (*p) {
    if (*p == '$' || *p == ',' || *p == '\r' || *p == '\n') {
      ++p;
      break;
    }

    if (i < len) {
      *dst++ = *p;
      ++i;
    }
    ++p;
  }

  *dst = 0;
  *src = p;

  return 1;
}

/**
 * @brief GSM 파서
 *
 * @param src
 * @param dst
 * @param len
 * @return uint8_t
 *
 * @warning 종료 조건을 AT 커맨드 보고 수정이 필요
 */
uint8_t parse_string_quoted(const char **src, char *dst, size_t len) {
  const char *p = *src;
  size_t i = 0;

  if (!dst) {
    return 0;
  }

  if (*p == ',') {
    ++p;
  }
  if (*p == '"') {
    ++p;
  }

  // 종료 문자 길이 확보
  if (len > 0) {
    --len;
  }

  while (*p) {
    if ((*p == '"' && (p[1] == ',' || p[1] == '\r' || p[1] == '\n')) ||
        (*p == '\r' || *p == '\n')) {
      ++p;
      break;
    }

    if (i < len) {
      *dst++ = *p;
      ++i;
    }
    ++p;
  }

  *dst = 0;
  *src = p;

  return 1;
}
