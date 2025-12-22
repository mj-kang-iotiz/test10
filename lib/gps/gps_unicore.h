#ifndef GPS_UNICORE_H
#define GPS_UNICORE_H

#include "gps_types.h"
#include <stdint.h>
#include <stdbool.h>

#define GPS_UNICORE_TERM_SIZE 32
#define GPS_UNICORE_BIN_HEADER_SIZE 24
#define GPS_UNICORE_BIN_SYNC_1 0xAA
#define GPS_UNICORE_BIN_SYNC_2 0x44
#define GPS_UNICORE_BIN_SYNC_3 0xB5

typedef enum {
  GPS_UNICORE_MSG_NONE = 0,
  GPS_UNICORE_MSG_COMMAND = 1
} gps_unicore_msg_t;

typedef enum {
  GPS_UNICORE_RESP_NONE = 0,
  GPS_UNICORE_RESP_OK = 1,
  GPS_UNICORE_RESP_ERROR = 2,
  GPS_UNICORE_RESP_UNKNOWN = 3
} gps_unicore_resp_t;

typedef struct {
  char term_str[GPS_UNICORE_TERM_SIZE];
  uint8_t term_pos;
  uint8_t term_num;

  gps_unicore_msg_t msg_type;
  uint8_t crc;
  uint8_t star;
  uint8_t colon; ///< '$이후부터 : 받을때 까지만 crc 검사 해야함! response: OK에서 스페이스바 부터 OK까지 포함하면 안됨
  gps_unicore_resp_t response;
} gps_unicore_parser_t;

typedef enum {
    GPS_UNICORE_BIN_MSG_BESTNAV = 2118
}gps_unicore_bin_msg_t;

typedef struct __attribute__((packed)) {
    uint8_t sync[3]; ///< 0xAA 0x44 0xB5
    uint8_t cpu_idle; ///< CPU idle 0-100
    uint16_t message_id;
    uint16_t message_len;
    uint8_t time_ref; ///< reference time (GPST or BDST)
    uint8_t time_status; ///< time status
    uint16_t wm; ///< week number
    uint32_t ms; ///< seconds of week (ms)
    uint32_t version; ///< release version
    uint8_t reserved;
    uint8_t leap_sec; ///< leap second
    uint16_t delay_ms; ///< output delay
} gps_unicore_bin_header_t;

typedef struct {
  gps_unicore_bin_header_t header;
  uint32_t crc32;
} gps_unicore_bin_parser_t;

typedef struct __attribute__((packed))
{
    uint32_t psol_status;
    uint32_t pos_type;
    double lat;
    double lon;
    double height;
    float geoid;
    uint32_t datum_id;
    float lat_dev; ///< latitude deviation
    float lon_dev;
    float height_dev;
    char base_station_id[4];
    float diff_age;
    float sol_age;
    uint8_t sv;
    uint8_t used_sv;
    uint8_t reserved[3];
    uint8_t ext_sol_stat;
    uint8_t galileo_bds3_sig_mask;
    uint8_t gps_glonass_bds2_sig_mask;
    uint32_t v_sol_status;
    uint32_t vel_type;
    float latency;
    float age;
    double hor_speed;
    double trk_gnd;
    double vert_speed;
    float verspd_std;
    float horspd_std;
}hpd_unicore_bestnavb_t;

typedef struct {
  hpd_unicore_bestnavb_t bestnav;
} gps_unicore_bin_data_t;

typedef struct gps_s gps_t;

gps_unicore_resp_t gps_get_unicore_response(gps_t *gps);
uint8_t gps_parse_unicore_term(gps_t *gps);
uint8_t gps_parse_unicore_bin(gps_t *gps);

#endif