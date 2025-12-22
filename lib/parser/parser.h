#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>
#include <stdio.h>

#define PARSER_CHAR_IS_NUM(x) ((x) >= '0' && (x) <= '9')
#define PARSER_CHAR_DEC_TO_NUM(x) ((x) - '0')
#define PARSER_CHAR_IS_HEX(x)                                                  \
  (((x) >= '0' && (x) <= '9') || ((x) >= 'a' && (x) <= 'f') ||                 \
   ((x) >= 'A' && (x) <= 'F'))
#define PARSER_CHAR_HEX_TO_NUM(x)                                              \
  (((x) >= '0' && (x) <= '9')                                                  \
       ? ((x) - '0')                                                           \
       : (((x) >= 'a' && (x) <= 'f')                                           \
              ? ((x) - 'a' + 10)                                               \
              : (((x) >= 'A' && (x) <= 'F') ? ((x) - 'A' + 10) : 0)))
#define PARSER_IS_CHAR(x)                                                      \
  (((x) >= 'a' && (x) <= 'z') || ((x) >= 'A' && (x) <= 'Z'))

int32_t parse_int32(const char **str);
uint32_t parse_uint32(const char **str);
uint32_t parse_hex(const char **str);
double parse_double(const char **str);
char parse_char(const char **str);
uint8_t parse_string(const char **src, char *dst, size_t len);
uint8_t parse_string_quoted(const char **src, char *dst, size_t len);

#endif
