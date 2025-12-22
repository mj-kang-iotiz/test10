#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include "FreeRTOS.h"
#include "gsm.h"
#include "queue.h"
#include "semphr.h"
#include <stdbool.h>
#include <stdint.h>


typedef struct tcp_socket_s tcp_socket_t;

/**
 * @brief TCP 소켓 생성
 *
 * @param gsm GSM 핸들
 * @param connect_id 소켓 ID (0-11)
 * @return tcp_socket_t* 소켓 핸들 (NULL이면 실패)
 */
tcp_socket_t *tcp_socket_create(gsm_t *gsm, uint8_t connect_id);

/**
 * @brief TCP 연결
 *
 * @param sock 소켓 핸들
 * @param context_id PDP context ID
 * @param remote_ip 서버 IP
 * @param remote_port 서버 포트
 * @param timeout_ms 타임아웃 (ms)
 * @return int 0: 성공, -1: 실패
 */
int tcp_connect(tcp_socket_t *sock, uint8_t context_id, const char *remote_ip,
                uint16_t remote_port, uint32_t timeout_ms);

/**
 * @brief TCP 데이터 전송
 *
 * @param sock 소켓 핸들
 * @param data 전송 데이터
 * @param len 데이터 길이
 * @return int 전송된 바이트 수 (-1이면 실패)
 */
int tcp_send(tcp_socket_t *sock, const uint8_t *data, size_t len);

/**
 * @brief 소켓 기본 수신 타임아웃 설정
 *
 * @param sock 소켓 핸들
 * @param timeout_ms 기본 타임아웃 (ms, 0=무한대기)
 */
void tcp_set_recv_timeout(tcp_socket_t *sock, uint32_t timeout_ms);

/**
 * @brief TCP 데이터 수신 (블로킹)
 *
 * @param sock 소켓 핸들
 * @param buf 수신 버퍼
 * @param len 버퍼 크기
 * @param timeout_ms 타임아웃 (ms, 0=소켓 기본값 사용)
 * @return int 수신된 바이트 수 (0=타임아웃, -1=에러)
 *
 * @note timeout_ms=0이면 소켓의 기본 timeout 사용
 *       기본값은 tcp_set_recv_timeout()으로 설정 (기본: 5000ms)
 */
int tcp_recv(tcp_socket_t *sock, uint8_t *buf, size_t len, uint32_t timeout_ms);

/**
 * @brief TCP 연결 종료
 *
 * @param sock 소켓 핸들
 * @return int 0: 성공, -1: 실패
 */
int tcp_close(tcp_socket_t *sock);
int tcp_close_force(tcp_socket_t *sock);

/**
 * @brief TCP 소켓 해제
 *
 * @param sock 소켓 핸들
 */
void tcp_socket_destroy(tcp_socket_t *sock);

/**
 * @brief 수신 가능한 데이터 크기 확인
 *
 * @param sock 소켓 핸들
 * @return size_t 수신 가능한 바이트 수
 */
size_t tcp_available(tcp_socket_t *sock);

gsm_tcp_state_t tcp_get_socket_state(tcp_socket_t *sock, uint8_t id);

#endif // TCP_SOCKET_H
