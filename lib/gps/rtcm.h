#ifndef RTCM_H
#define RTCM_H

#include <stdint.h>
#include <stdbool.h>
#include "gps.h"

typedef struct
{
    uint16_t msg_len;      // RTCM 메시지 길이 (10비트)
    uint16_t msg_type;     // RTCM 메시지 타입 (12비트)
    uint16_t payload_cnt;  // 현재까지 받은 페이로드 바이트 수
    uint16_t total_len;    // 전체 패킷 길이 (헤더 3 + 페이로드 + CRC 3)
}gps_rtcm_parser_t;

/**
 * @brief RTCM 전송 초기화 (task 없음)
 *
 * RTCM 데이터를 비동기로 LoRa TX task를 통해 전송
 * 별도의 RTCM task 없이 LoRa TX task를 직접 사용
 */
void rtcm_tx_task_init(void);

/**
 * @brief RTCM 데이터를 LoRa로 전송 (비동기, 자동 분할)
 *
 * - 완전 비동기 전송: 즉시 리턴 (GPS Task 블록 안 됨)
 * - HEX ASCII 변환으로 인해 최대 118바이트씩 전송
 * - 118바이트 초과 시 자동으로 여러 fragment로 분할
 * - ToA(Time on Air) 자동 계산: (bytes / 118) * 350ms * 1.2
 * - 모든 fragment를 LoRa TX 큐에 추가 (순차 처리)
 * - 모든 RTCM 타입 전송 (1074, 1084, 1124 등)
 * - LoRa TX 큐가 가득 찬 경우에만 실패
 *
 * @param gps GPS 핸들
 * @return true: 큐 추가 성공, false: 큐 full 또는 에러
 */
bool rtcm_send_to_lora(gps_t *gps);

#endif
