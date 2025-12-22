#include "ntrip_app.h"
#include "FreeRTOS.h"
#include "gps_app.h"
#include "led.h"
#include "queue.h"
#include "task.h"
#include "tcp_socket.h"
#include "flash_params.h"
#include <stdlib.h>
#include <string.h>
#include "base_auto_fix.h"
#include "rs485_app.h"

#ifndef TAG
#define TAG "NTRIP"
#endif

#include "log.h"

#define NTRIP_CONNECT_ID 0 // 소켓 ID (0-11)
#define NTRIP_CONTEXT_ID 1 // PDP context ID

#define NTRIP_MAX_CONNECT_RETRY 3
#define NTRIP_MAX_TIMEOUT_COUNT 3    // 연속 타임아웃 최대 허용 횟수
#define NTRIP_RECONNECT_DELAY_MS 500 // 재연결 대기 시간 (ms)

#define NTRIP_GGA_QUEUE_SIZE 10 // GGA 전송 큐 크기 (재연결 중 버퍼링)
#define NTRIP_GGA_MAX_LEN 100   // GGA 문장 최대 길이
#define NTRIP_GGA_SEND_BATCH 5  // 한 루프당 최대 GGA 전송 개수 (GSM 버퍼 보호)
#define NTRIP_INIT_RETRY_BASE_DELAY_MS 1000 // 초기화 재시도 기본 대기 시간 (백오프)
#define NTRIP_INIT_MAX_RETRY 3


// GGA 전송 큐 아이템
typedef struct
{
  char data[NTRIP_GGA_MAX_LEN];
  uint8_t len;
} ntrip_gga_queue_item_t;

static TaskHandle_t g_ntrip_recv_task_handle = NULL;

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_encode(const char *input, size_t input_len, char *output, size_t output_size)
{

  size_t output_len = 4 * ((input_len + 2) / 3);

  if (output_size < output_len + 1)
  {

    return -1; // 버퍼 부족
  }

  size_t i, j;

  for (i = 0, j = 0; i < input_len;)
  {
    uint32_t octet_a = i < input_len ? (unsigned char)input[i++] : 0;
    uint32_t octet_b = i < input_len ? (unsigned char)input[i++] : 0;
    uint32_t octet_c = i < input_len ? (unsigned char)input[i++] : 0;
    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
    output[j++] = base64_table[(triple >> 18) & 0x3F];
    output[j++] = base64_table[(triple >> 12) & 0x3F];
    output[j++] = base64_table[(triple >> 6) & 0x3F];
    output[j++] = base64_table[triple & 0x3F];
  }

  size_t mod = input_len % 3;

  if (mod == 1)
  {
    output[output_len - 2] = '=';
    output[output_len - 1] = '=';
  }
  else if (mod == 2)
  {
    output[output_len - 1] = '=';
  }

  output[output_len] = '\0';

  return output_len;
}

/**
 * @brief NTRIP HTTP 요청 문자열 생성
 * @param buffer 출력 버퍼
 * @param buffer_size 버퍼 크기
 * @return 생성된 문자열 길이 (실패시 -1)
 */

static int ntrip_build_http_request(char *buffer, size_t buffer_size)
{

  user_params_t *params = flash_params_get_current();

  // ID:PW 문자열 생성

  char credentials[128];

  snprintf(credentials, sizeof(credentials), "%s:%s", params->ntrip_id, params->ntrip_pw);

  // Base64 인코딩

  char encoded_credentials[256];

  int encoded_len = base64_encode(credentials, strlen(credentials), encoded_credentials, sizeof(encoded_credentials));

  if (encoded_len < 0)
  {

    LOG_ERR("Base64 인코딩 실패");

    return -1;
  }

  // HTTP 요청 생성

  int len = snprintf(buffer, buffer_size,
                     "GET /%s HTTP/1.1\r\n"
                     "User-Agent: NTRIP GUGU SYSTEM\r\n"
                     "Accept: */*\r\n"
                     "Connection: keep-alive\r\n"
                     "Authorization: Basic %s\r\n"
                     "\r\n",
                     params->ntrip_mountpoint,
                     encoded_credentials);

  if (len < 0 || len >= buffer_size)
  {

    LOG_ERR("HTTP 요청 생성 실패");

    return -1;
  }

  return len;
}

__attribute__((section(".ccmram"))) uint8_t recv_buf[1500];

__attribute__((section(".ccmram"))) static char g_ntrip_http_request[512]; // 동적으로 생성된 HTTP 요청 저장

// NTRIP TCP 소켓 (GGA 전송용)
static tcp_socket_t *g_ntrip_socket = NULL;
static bool g_ntrip_connected = false;

// GGA 전송 큐
static QueueHandle_t g_gga_send_queue = NULL;

// GGA 송신 태스크 핸들
static TaskHandle_t g_gga_send_task_handle = NULL;

/**
 * @brief NTRIP 서버에 연결하고 HTTP 요청/응답 처리
 * @return 0: 성공, -1: 실패
 */
static int ntrip_connect_to_server(tcp_socket_t *sock)
{
  int ret;
  int retry_count = 0;

  user_params_t *params = flash_params_get_current();
  int ntrip_port = atoi(params->ntrip_port);

  // HTTP 요청 생성
  if (ntrip_build_http_request(g_ntrip_http_request, sizeof(g_ntrip_http_request)) < 0)
  {
    LOG_ERR("HTTP 요청 생성 실패");
    return -1;
  }

  LOG_DEBUG("HTTP 요청: %s", g_ntrip_http_request);

  while (retry_count < NTRIP_MAX_CONNECT_RETRY)
  {
    LOG_INFO("NTRIP 서버 연결 시도 [%d/%d]: %s:%d", retry_count + 1,
             NTRIP_MAX_CONNECT_RETRY, params->ntrip_url, ntrip_port);

    led_set_color(LED_ID_1, LED_COLOR_YELLOW);  // 연결 시도 중

    // TCP 연결
    ret = tcp_connect(sock, NTRIP_CONTEXT_ID, params->ntrip_url, ntrip_port, 10000);

    if (ret != 0 || tcp_get_socket_state(sock, NTRIP_CONNECT_ID) != GSM_TCP_STATE_CONNECTED)
    {
      LOG_WARN("TCP 연결 실패 (ret=%d), 강제 닫기 후 재시도...", ret);
      led_set_color(LED_ID_1, LED_COLOR_RED);  // 연결 실패
      tcp_close_force(sock);
      vTaskDelay(pdMS_TO_TICKS(1000));
      retry_count++;
      continue;
    }

    LOG_DEBUG("TCP 연결 성공");

    // HTTP 요청 전송
    ret = tcp_send(sock, (const uint8_t *)g_ntrip_http_request, strlen(g_ntrip_http_request));
    if (ret < 0)
    {
      LOG_WARN("HTTP 요청 전송 실패: %d", ret);
      tcp_close_force(sock);
      vTaskDelay(pdMS_TO_TICKS(1000));
      retry_count++;
      continue;
    }

    LOG_DEBUG("HTTP 요청 전송 완료");

    // HTTP 응답 수신
    ret = tcp_recv(sock, recv_buf, sizeof(recv_buf), 0);
    if (ret <= 0)
    {
      LOG_WARN("HTTP 응답 수신 실패: %d", ret);
      tcp_close_force(sock);
      vTaskDelay(pdMS_TO_TICKS(1000));
      retry_count++;
      continue;
    }

    // HTTP 응답 검증
    recv_buf[ret] = '\0';
    int log_len = ret > 200 ? 200 : ret;
    LOG_DEBUG("HTTP 응답: %.*s", log_len, recv_buf);

    if (strncmp((char *)recv_buf, "ICY 200", 7) == 0 ||
        strstr((char *)recv_buf, "HTTP/1.0 200") != NULL ||
        strstr((char *)recv_buf, "HTTP/1.1 200") != NULL)
    {
      LOG_INFO("NTRIP 서버 응답: 200 OK");
      return 0; // 성공
    }

    // 에러 응답 처리
    char *status_line = strtok((char *)recv_buf, "\r\n");
    if (status_line)
    {
      LOG_ERR("NTRIP 서버 에러 응답: %s", status_line);
      if (strstr(status_line, "401"))
      {
        LOG_ERR("인증 실패 - ID/PW 확인 필요");
      }
      else if (strstr(status_line, "404"))
      {
        LOG_ERR("마운트포인트를 찾을 수 없음");
      }
    }

    tcp_close_force(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
    retry_count++;
  }

  LOG_ERR("NTRIP 서버 연결 최대 재시도 횟수 초과");
  return -1;
}

/**
 * @brief GGA 송신 전용 태스크
 *
 * GGA 큐에서 데이터를 블로킹 대기하여 즉시 전송
 * - 폴링 없이 이벤트 기반 동작
 * - RTCM 수신과 완전히 독립적으로 동작
 */
static void ntrip_gga_send_task(void *pvParameter)
{
  tcp_socket_t *sock = (tcp_socket_t *)pvParameter;
  ntrip_gga_queue_item_t gga_item;

  LOG_INFO("GGA 송신 태스크 시작");

  while (1)
  {
    // GGA 큐에서 블로킹 대기 (데이터 도착 즉시 깨어남)
    if (xQueueReceive(g_gga_send_queue, &gga_item, portMAX_DELAY) == pdTRUE)
    {

      // 연결 상태 확인
      if (!g_ntrip_connected)
      {
        LOG_DEBUG("NTRIP 소켓 연결 안됨");
        continue;
      }

      // GGA 전송
      int ret = tcp_send(sock, (const uint8_t *)gga_item.data, gga_item.len);

      if (ret > 0)
      {
        LOG_INFO("GGA 전송 완료 (%d bytes): %.*s",
                 gga_item.len, gga_item.len, gga_item.data);
      }
      else
      {
        LOG_WARN("GGA 전송 실패: %d", ret);
        // 전송 실패해도 계속 진행 (다음 GGA는 다시 시도)
        // 연결이 끊어진 경우 수신 태스크에서 재연결 처리
      }
    }
  }
}

/**
 * @brief NTRIP TCP 수신 태스크
 */
static void ntrip_tcp_recv_task(void *pvParameter)
{
  gsm_t *gsm = (gsm_t *)pvParameter;
  tcp_socket_t *sock = NULL;
  gps_t *gps_handle = gps_get_instance_handle(0);

  int ret;
  int timeout_count = 0;   // 연속 타임아웃 카운터
  int reconnect_count = 0; // 총 재연결 시도 횟수

  LOG_INFO("NTRIP 태스크 시작");
  led_set_color(LED_ID_1, LED_COLOR_YELLOW);  // 연결 시도 중

   if (!g_gga_send_queue)

  {

    int queue_retry = 0;

    while (queue_retry < NTRIP_INIT_MAX_RETRY)

    {

      g_gga_send_queue = xQueueCreate(NTRIP_GGA_QUEUE_SIZE, sizeof(ntrip_gga_queue_item_t));

      if (g_gga_send_queue)

      {

        LOG_INFO("GGA 전송 큐 생성 완료");

        break;

      }

 

      queue_retry++;

      LOG_ERR("GGA 전송 큐 생성 실패, 재시도 중... (%d/%d)", queue_retry, NTRIP_INIT_MAX_RETRY);

      vTaskDelay(pdMS_TO_TICKS(NTRIP_INIT_RETRY_BASE_DELAY_MS * queue_retry));

    }

 

    if (!g_gga_send_queue)

    {

      LOG_ERR("GGA 전송 큐 생성 최종 실패 - 태스크 종료");
      led_set_color(LED_ID_1, LED_COLOR_RED); 
      g_ntrip_recv_task_handle = NULL; 
      vTaskDelete(NULL);

      return;

    }

  }

 

  // ========================================

  // 2단계: 초기 연결 (소켓 생성 + 서버 연결 + HTTP 헤더 전송) - 재시도

  // ========================================

  int init_retry = 0;

  bool init_success = false;

 

  while (init_retry < NTRIP_INIT_MAX_RETRY && !init_success)

  {

    LOG_INFO("초기 연결 시도 (%d/%d)", init_retry + 1, NTRIP_INIT_MAX_RETRY);

 

    // 2-1. TCP 소켓 생성

    sock = tcp_socket_create(gsm, NTRIP_CONNECT_ID);

    if (!sock)

    {

      init_retry++;

      LOG_ERR("TCP 소켓 생성 실패, 재시도 대기... (%d/%d)", init_retry, NTRIP_INIT_MAX_RETRY);

      vTaskDelay(pdMS_TO_TICKS(NTRIP_INIT_RETRY_BASE_DELAY_MS * init_retry));

      continue;

    }

    LOG_INFO("TCP 소켓 생성 완료");

    g_ntrip_socket = sock;

 

    // 2-2. 서버 연결 (TCP + HTTP 요청/응답)
    if (ntrip_connect_to_server(sock) != 0)
    {
      init_retry++;
      LOG_ERR("서버 연결 실패, 재시도 대기... (%d/%d)", init_retry, NTRIP_INIT_MAX_RETRY);
      tcp_socket_destroy(sock);
      sock = NULL;
      g_ntrip_socket = NULL;
      vTaskDelay(pdMS_TO_TICKS(NTRIP_INIT_RETRY_BASE_DELAY_MS * init_retry));
      continue;
    }

    LOG_INFO("서버 연결 완료");

 

    // 모든 초기화 단계 성공

    init_success = true;

  }

 

  // 초기화 최종 실패 확인

  if (!init_success)

  {

    LOG_ERR("초기 연결 최종 실패 - 태스크 종료");
led_set_color(LED_ID_1, LED_COLOR_RED); 
    if (sock)

    {

      tcp_socket_destroy(sock);

      g_ntrip_socket = NULL;

    }
    g_ntrip_recv_task_handle = NULL;

    // board_config_t* config = board_get_config();
    // if(config->use_rs485)
    // {
    //   RS485_Send("")
    // }
    vTaskDelete(NULL);

    return;

  }

 

  // ========================================

  // 3단계: 연결 성공 후 상태 설정

  // ========================================

  LOG_INFO("NTRIP 초기 연결 완료!");

  g_ntrip_connected = true;

  base_auto_fix_on_ntrip_connected(true);

  led_set_color(LED_ID_1, LED_COLOR_GREEN);

 

  // GGA 송신 태스크 생성

  if (g_gga_send_task_handle == NULL)

  {

    xTaskCreate(ntrip_gga_send_task, "gga_send", 1024, sock,

                tskIDLE_PRIORITY + 2, &g_gga_send_task_handle);

    LOG_INFO("GGA 송신 태스크 생성 완료");

  }

  while (1)
  {
    ret = tcp_recv(sock, recv_buf, sizeof(recv_buf), 0);

    if (ret > 0)
    {
      // 수신 성공
      led_set_color(LED_ID_1, LED_COLOR_GREEN);
      timeout_count = 0;
      LOG_DEBUG("수신 데이터 (%d bytes):", ret);

      gps_handle->ops->send((const char *)recv_buf, ret);
    }
    else if (ret == 0)
    {
      // 타임아웃
      led_set_color(LED_ID_1, LED_COLOR_YELLOW);
      timeout_count++;

      LOG_WARN("수신 타임아웃 (%d/%d)", timeout_count, NTRIP_MAX_TIMEOUT_COUNT);

      // 소켓 상태 확인
      gsm_tcp_state_t state = tcp_get_socket_state(sock, NTRIP_CONNECT_ID);
      if (state == GSM_TCP_STATE_CLOSING || state == GSM_TCP_STATE_CLOSED)
      {
        LOG_ERR("소켓 상태 비정상 (state=%d), 재연결 필요", state);
        timeout_count = NTRIP_MAX_TIMEOUT_COUNT; // 즉시 재연결
      }

      // 연속 타임아웃 최대 횟수 초과 시 재연결
      if (timeout_count >= NTRIP_MAX_TIMEOUT_COUNT)
      {
        // 재연결 시작 전 큐 상태 확인
        UBaseType_t queued_gga = uxQueueMessagesWaiting(g_gga_send_queue);
        LOG_WARN("소켓 재연결 시도 (큐에 GGA %lu개 대기중)", queued_gga);
        led_set_color(LED_ID_1, LED_COLOR_RED);

        g_ntrip_connected = false;

        tcp_close_force(sock);
        vTaskDelay(pdMS_TO_TICKS(NTRIP_RECONNECT_DELAY_MS));

        // 재연결 시도
        if (ntrip_connect_to_server(sock) != 0)
        {
          reconnect_count++;
          LOG_ERR("재연결 실패 (%d회)", reconnect_count);

          // 재연결 실패 시 더 긴 대기
          vTaskDelay(pdMS_TO_TICKS(NTRIP_RECONNECT_DELAY_MS * 2));
        }
        else
        {
          led_set_color(LED_ID_1, LED_COLOR_GREEN);
          timeout_count = 0; // 타임아웃 카운터 리셋

          // 재연결 중 쌓인 오래된 GGA 버리기 (최신 1개만 유지)
          UBaseType_t queued_gga = uxQueueMessagesWaiting(g_gga_send_queue);
          if (queued_gga > 1)
          {
            LOG_INFO("재연결 완료");

            ntrip_gga_queue_item_t old_item;
            while (queued_gga > 1)
            {
              xQueueReceive(g_gga_send_queue, &old_item, 0);
              queued_gga--;
            }
          }
          else if (queued_gga == 1)
          {
            LOG_INFO("재연결 완료");
          }

          g_ntrip_connected = true;
          base_auto_fix_on_ntrip_connected(true);
        }
      }
    }
    else
    {
      // 에러
      LOG_ERR("수신 에러: %d", ret);

      // 소켓 상태 확인
      gsm_tcp_state_t state = tcp_get_socket_state(sock, NTRIP_CONNECT_ID);
      LOG_ERR("현재 소켓 상태: %d", state);

      led_set_color(1, LED_COLOR_RED);

      // ★ 연결 상태만 false로 설정 (태스크는 살려둠)
      g_ntrip_connected = false;

      tcp_close_force(sock);
      vTaskDelay(pdMS_TO_TICKS(NTRIP_RECONNECT_DELAY_MS));

      if (ntrip_connect_to_server(sock) != 0)
      {
        reconnect_count++;
        LOG_ERR("재연결 실패 (%d회)", reconnect_count);
        vTaskDelay(pdMS_TO_TICKS(NTRIP_RECONNECT_DELAY_MS * 2));
      }
      else
      {
        LOG_INFO("재연결 성공");
        timeout_count = 0;
        led_set_color(LED_ID_1, LED_COLOR_GREEN);
        // 재연결 중 쌓인 오래된 GGA 버리기 (최신 1개만 유지)
        UBaseType_t queued_gga = uxQueueMessagesWaiting(g_gga_send_queue);
        if (queued_gga > 1)
        {
          LOG_INFO("재연결 완료");

          // 오래된 데이터 모두 제거
          ntrip_gga_queue_item_t old_item;
          while (queued_gga > 1)
          {
            xQueueReceive(g_gga_send_queue, &old_item, 0);
            queued_gga--;
          }
        }
        else if (queued_gga == 1)
        {
          LOG_INFO("재연결 완료");
        }

        g_ntrip_connected = true;
        base_auto_fix_on_ntrip_connected(true);
      }
    }
  }

  LOG_WARN("TCP 연결 종료");

  // GGA 송신 태스크 삭제
  if (g_gga_send_task_handle != NULL)
  {
    vTaskDelete(g_gga_send_task_handle);
    g_gga_send_task_handle = NULL;
    LOG_WARN("GGA 송신 태스크 삭제");
  }

  tcp_close(sock);
  tcp_socket_destroy(sock);
  g_ntrip_recv_task_handle = NULL; 
  vTaskDelete(NULL);
}

void ntrip_task_create(gsm_t *gsm)
{
  xTaskCreate(ntrip_tcp_recv_task, "ntrip_recv", 1536, gsm,
              tskIDLE_PRIORITY + 3, &g_ntrip_recv_task_handle);
}

int ntrip_send_gga_data(const char *data, uint8_t len)
{
  if (!data || len == 0 || len >= NTRIP_GGA_MAX_LEN)
  {
    LOG_ERR("유효하지 않은 GGA (len=%d)", len);
    return -1;
  }

  if (!g_gga_send_queue)
  {
    LOG_WARN("GGA 큐 초기화 필요");
    return -2;
  }

  ntrip_gga_queue_item_t item;
  memcpy(item.data, data, len);
  item.data[len] = '\0';
  item.len = len;

  // 큐에 넣기 (블로킹 안 함, 큐가 가득 차면 실패)
  if (xQueueSend(g_gga_send_queue, &item, 0) != pdTRUE)
  {
    // 큐가 가득 참 - 오래된 데이터 하나 제거하고 다시 시도
    ntrip_gga_queue_item_t old_item;
    xQueueReceive(g_gga_send_queue, &old_item, 0);

    if (xQueueSend(g_gga_send_queue, &item, 0) != pdTRUE)
    {
      LOG_WARN("GGA 전송 큐 가득참");
      return -3;
    }
    LOG_DEBUG("GGA 전송 큐가 가득차 오래된 데이터 버림");
  }

  return len;
}


bool ntrip_gga_send_queue_initialized(void)
{
  return g_gga_send_queue != NULL;
}

void ntrip_stop(void)
{
    LOG_INFO("NTRIP 중지 시작...");

  g_ntrip_connected = false;

 

  // ★ 중요: 태스크를 먼저 삭제한 후 소켓/큐를 정리해야 함

 

  // 1. 수신 태스크 삭제 (tcp_recv를 더 이상 호출하지 않도록)

  if (g_ntrip_recv_task_handle != NULL)

  {

    // 태스크 유효성 확인 후 삭제

    eTaskState state = eTaskGetState(g_ntrip_recv_task_handle);

    if (state != eDeleted && state != eInvalid)

    {

      vTaskDelete(g_ntrip_recv_task_handle);

      LOG_INFO("NTRIP 수신 태스크 삭제");

    }

    else

    {

      LOG_INFO("NTRIP 수신 태스크 이미 종료됨");

    }

    g_ntrip_recv_task_handle = NULL;

  }

 

  // 2. GGA 송신 태스크 삭제

  if (g_gga_send_task_handle != NULL)

  {

    // 태스크 유효성 확인 후 삭제

    eTaskState state = eTaskGetState(g_gga_send_task_handle);

    if (state != eDeleted && state != eInvalid)

    {

      vTaskDelete(g_gga_send_task_handle);

      LOG_INFO("GGA 송신 태스크 삭제");

    }

    else

    {

      LOG_INFO("GGA 송신 태스크 이미 종료됨");

    }

    g_gga_send_task_handle = NULL;

  }

 
 

  // 3. 모든 태스크가 종료된 후 소켓 정리

  if (g_ntrip_socket != NULL)

  {

    tcp_close_force(g_ntrip_socket);

    tcp_socket_destroy(g_ntrip_socket);

    g_ntrip_socket = NULL;

    LOG_INFO("NTRIP 소켓 닫기 및 파괴");

  }

 

  // 4. GGA 큐 정리 (큐 삭제는 하지 않음, 재시작 시 재사용)

  if (g_gga_send_queue != NULL)

  {

    xQueueReset(g_gga_send_queue);

    LOG_INFO("GGA 큐 리셋");

  }

  led_set_color(LED_ID_1, LED_COLOR_NONE);

  LOG_INFO("NTRIP 중지 완료");
}

bool ntrip_is_connected(void)

{

  return g_ntrip_connected;

}