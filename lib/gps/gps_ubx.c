#include "gps_ubx.h"
#include "gps.h"
#include "gps_parse.h"
#include <string.h>

#define UBX_SYNC_1 0xB5
#define UBX_SYNC_2 0x62

static inline void calc_ubx_chksum(gps_t *gps);
static inline uint8_t check_ubx_chksum(gps_t *gps);
static void store_ubx_nav_data(gps_t *gps);
static void store_ubx_data(gps_t *gps);
static void handle_ubx_ack(gps_t *gps, uint8_t cls, uint8_t id, bool is_ack);
static size_t ubx_build_valset_msg(uint8_t *buf, ubx_cfg_layer_t layer,
                                   const ubx_cfg_item_t *items, size_t item_count);
static uint32_t get_tick_ms(void);
static void store_ubx_ack_data(gps_t *gps);


static inline void calc_ubx_chksum(gps_t *gps)
{
  gps->ubx.cal_chksum_a = 0;
  gps->ubx.cal_chksum_b = 0;

  for (int i = 0; i < gps->ubx.len + 4; i++)
  {
    gps->ubx.cal_chksum_a += gps->payload[i];
    gps->ubx.cal_chksum_b += gps->ubx.cal_chksum_a;
  }
}

/**
 * @brief ubx 프로토콜 체크섬 확인
 *
 * @param[in] gps
 * @return uint8_t 1: success, 0: fail
 */
static inline uint8_t check_ubx_chksum(gps_t *gps)
{
  calc_ubx_chksum(gps);

  if (gps->ubx.cal_chksum_a == gps->ubx.chksum_a &&
      gps->ubx.cal_chksum_b == gps->ubx.chksum_b)
  {
    return 1;
  }

  return 0;
}

/**
 * @brief 파싱한 ubx nav 프토토콜 데이터 저장
 *
 * @param[inout] gps
 */
static void store_ubx_nav_data(gps_t *gps)
{
  char *data = &gps->payload[4];
  switch (gps->ubx.id)
  {
  case GPS_UBX_NAV_ID_HPPOSLLH:
    memcpy(&gps->ubx_data.hpposllh, data, sizeof(gps_ubx_nav_hpposllh_t));
    break;

  case GPS_UBX_NAV_ID_RELPOSNED:
    memcpy(&gps->ubx_data.relposned, data, sizeof(gps_ubx_nav_relposned_t));
    break;

  default:
    break;
  }
}

/**
 * @brief 파싱한 ubx 프로토콜 데이터 저장
 *
 * @param[inout] gps
 */
static void store_ubx_data(gps_t *gps)
{
  switch (gps->ubx.class)
  {
  case GPS_UBX_CLASS_NAV:
    store_ubx_nav_data(gps);
    break;

  case GPS_UBX_CLASS_ACK:
    store_ubx_ack_data(gps);
    break;

  default:
    break;
  }
}

/**
 * @brief ubx 프로토콜 파싱
 *
 * @param[inout] gps
 * @return uint8_t 1: success 0: checksum mismatch
 */
uint8_t gps_parse_ubx(gps_t *gps)
{
  if (gps->pos == 1)
  {
    gps->ubx.class = gps->payload[0];
    gps->state = GPS_PARSE_STATE_UBX_MSG_CLASS;
  }
  else if (gps->pos == 2)
  {
    gps->ubx.id = gps->payload[1];
    gps->state = GPS_PARSE_STATE_UBX_MSG_ID;
  }
  else if (gps->pos == 4)
  {
    gps->ubx.len = (gps->payload[2] | ((gps->payload[3] << 8)));
    gps->state = GPS_PARSE_STATE_UBX_LEN;
  }
  else
  {
    if (gps->pos <= 4 + gps->ubx.len)
    {
      gps->state = GPS_PARSE_STATE_UBX_PAYLOAD;
    }
    else if (gps->pos == 5 + gps->ubx.len)
    {
      gps->state = GPS_PARSE_STATE_UBX_CHKSUM_A;
      gps->ubx.chksum_a = gps->payload[4 + gps->ubx.len];
    }
    else if (gps->pos == 6 + gps->ubx.len)
    {
      gps->state = GPS_PARSE_STATE_UBX_CHKSUM_B;
      gps->ubx.chksum_b = gps->payload[5 + gps->ubx.len];

      if (check_ubx_chksum(gps))
      {
        store_ubx_data(gps);
        gps_msg_t msg;
        msg.ubx.class = gps->ubx.class;
        msg.ubx.id = gps->ubx.id;
        gps->handler(gps, GPS_EVENT_NONE, GPS_PROTOCOL_UBX, msg);
        gps->protocol = GPS_PROTOCOL_NONE;
        gps->state = GPS_PARSE_STATE_NONE;

        return 1;
      }
      else
      {
        gps->protocol = GPS_PROTOCOL_NONE;
        gps->state = GPS_PARSE_STATE_NONE;
        return 0;
      }
    }
  }

  return 1;
}

/**

 * @brief 현재 tick을 ms로 반환

 *

 * @return uint32_t tick (ms)

 */

static uint32_t get_tick_ms(void)
{

  return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/**

 * @brief UBX checksum 계산

 *

 * @param[in] data Checksum 계산할 데이터 (Class부터 시작)

 * @param[in] len 데이터 길이

 * @param[out] ck_a Checksum A

 * @param[out] ck_b Checksum B

 */

void ubx_calc_checksum(const uint8_t *data, size_t len,

                       uint8_t *ck_a, uint8_t *ck_b)
{

  *ck_a = 0;

  *ck_b = 0;

  for (size_t i = 0; i < len; i++)
  {

    *ck_a += data[i];

    *ck_b += *ck_a;
  }
}

/**

 * @brief UBX command handler 초기화

 *

 * @param[inout] handler Command handler

 */

void ubx_cmd_handler_init(ubx_cmd_handler_t *handler)
{

  handler->pending_cls = 0;

  handler->pending_id = 0;

  handler->state = UBX_CMD_STATE_IDLE;

  handler->timestamp = 0;

  handler->callback = NULL;

  handler->callback_data = NULL;
}

/**

 * @brief 명령 상태 확인 (타임아웃 체크 포함)

 *

 * @param[inout] handler Command handler

 * @param[in] timeout_ms Timeout (ms)

 * @return ubx_cmd_state_t 현재 상태

 */

ubx_cmd_state_t ubx_get_cmd_state(ubx_cmd_handler_t *handler, uint32_t timeout_ms)
{

  if (handler->state == UBX_CMD_STATE_WAITING)
  {

    uint32_t elapsed = get_tick_ms() - handler->timestamp;

    if (elapsed >= timeout_ms)
    {

      handler->state = UBX_CMD_STATE_TIMEOUT;
    }
  }

  return handler->state;
}

/**

 * @brief ACK/NAK 처리 (파서에서 호출)

 *

 * @param[inout] gps GPS 구조체

 * @param[in] cls ACK된 명령의 Class

 * @param[in] id ACK된 명령의 ID

 * @param[in] is_ack true: ACK, false: NAK

 */

static void handle_ubx_ack(gps_t *gps, uint8_t cls, uint8_t id, bool is_ack)
{

  ubx_cmd_handler_t *handler = &gps->ubx_cmd_handler;

  // Pending 명령과 일치하는지 확인

  if (handler->state == UBX_CMD_STATE_WAITING &&

      handler->pending_cls == cls &&

      handler->pending_id == id)
  {

    // 상태 업데이트

    handler->state = is_ack ? UBX_CMD_STATE_ACK : UBX_CMD_STATE_NAK;

    // 콜백 호출 (있으면)

    if (handler->callback)
    {

      handler->callback(is_ack, handler->callback_data);

      handler->callback = NULL;

      handler->callback_data = NULL;
    }
  }
}

/**

 * @brief UBX VAL-SET 메시지 생성

 *

 * @param[out] buf 메시지 버퍼

 * @param[in] layer Layer (RAM/BBR/Flash)

 * @param[in] items Configuration items

 * @param[in] item_count Item 개수

 * @return size_t 생성된 메시지 길이

 */

static size_t ubx_build_valset_msg(uint8_t *buf, ubx_cfg_layer_t layer,

                                   const ubx_cfg_item_t *items, size_t item_count)
{

  size_t offset = 0;

  // Sync bytes

  buf[offset++] = UBX_SYNC_1;

  buf[offset++] = UBX_SYNC_2;

  // Class & ID

  buf[offset++] = GPS_UBX_CLASS_CFG;

  buf[offset++] = GPS_UBX_CFG_ID_VALSET;

  // Payload length (계산 후 설정)

  size_t payload_start = offset;

  offset += 2; // Length 필드 공간 확보

  size_t payload_offset = offset;

  // VAL-SET 헤더

  buf[offset++] = 0x00; // Version

  buf[offset++] = (uint8_t)layer; // Layer

  buf[offset++] = 0x00; // Reserved

  buf[offset++] = 0x00; // Reserved

  // Key/Value 쌍 추가

  for (size_t i = 0; i < item_count; i++)
  {

    if (items[i].value_len > 8 || items[i].value_len == 0)
    {

      return 0; // 잘못된 value_len
    }
    // Key (4 bytes, little-endian)

    buf[offset++] = (items[i].key_id >> 0) & 0xFF;

    buf[offset++] = (items[i].key_id >> 8) & 0xFF;

    buf[offset++] = (items[i].key_id >> 16) & 0xFF;

    buf[offset++] = (items[i].key_id >> 24) & 0xFF;

    // Value

    memcpy(&buf[offset], items[i].value, items[i].value_len);

    offset += items[i].value_len;
  }

  // Payload length 설정

  uint16_t payload_len = offset - payload_offset;

  buf[payload_start] = payload_len & 0xFF;

  buf[payload_start + 1] = (payload_len >> 8) & 0xFF;

  // Checksum 계산

  uint8_t ck_a, ck_b;

  ubx_calc_checksum(&buf[2], offset - 2, &ck_a, &ck_b);

  buf[offset++] = ck_a;

  buf[offset++] = ck_b;

  return offset;
}

/**

 * @brief UBX VAL-SET 전송 (비동기)

 *

 * @param[inout] gps GPS 구조체

 * @param[in] layer Layer (RAM/BBR/Flash)

 * @param[in] items Configuration items

 * @param[in] item_count Item 개수

 * @return true 전송 성공, false 이미 대기 중인 명령 있음

 */

bool ubx_send_valset(gps_t *gps, ubx_cfg_layer_t layer,

                     const ubx_cfg_item_t *items, size_t item_count)
{

  ubx_cmd_handler_t *handler = &gps->ubx_cmd_handler;

  // 이미 대기 중인 명령이 있으면 실패

  if (handler->state == UBX_CMD_STATE_WAITING)
  {

    return false;
  }

  // 메시지 생성

  uint8_t msg[256];

  size_t msg_len = ubx_build_valset_msg(msg, layer, items, item_count);

  if (msg_len == 0)
  {

    return false;
  }

  // Pending 설정

  handler->pending_cls = GPS_UBX_CLASS_CFG;

  handler->pending_id = GPS_UBX_CFG_ID_VALSET;

  handler->state = UBX_CMD_STATE_WAITING;

  handler->timestamp = get_tick_ms();

  // UART로 전송

  gps->ops->send((const char *)msg, msg_len);

  return true;
}

/**

 * @brief UBX VAL-SET 전송 (콜백 방식)

 *

 * @param[inout] gps GPS 구조체

 * @param[in] layer Layer (RAM/BBR/Flash)

 * @param[in] items Configuration items

 * @param[in] item_count Item 개수

 * @param[in] callback ACK/NAK 콜백

 * @param[in] user_data 콜백 데이터

 * @return true 전송 성공, false 실패

 */

bool ubx_send_valset_cb(gps_t *gps, ubx_cfg_layer_t layer,

                        const ubx_cfg_item_t *items, size_t item_count,

                        ubx_ack_callback_t callback, void *user_data)
{

  if (!ubx_send_valset(gps, layer, items, item_count))
  {

    return false;
  }

  // 콜백 등록

  gps->ubx_cmd_handler.callback = callback;

  gps->ubx_cmd_handler.callback_data = user_data;

  return true;
}

/**

 * @brief UBX VAL-SET 전송 (동기 방식)

 *

 * @param[inout] gps GPS 구조체

 * @param[in] layer Layer (RAM/BBR/Flash)

 * @param[in] items Configuration items

 * @param[in] item_count Item 개수

 * @param[in] timeout_ms Timeout (ms)

 * @return true ACK 받음, false NAK 또는 타임아웃

 */

bool ubx_send_valset_sync(gps_t *gps, ubx_cfg_layer_t layer,

                          const ubx_cfg_item_t *items, size_t item_count,

                          uint32_t timeout_ms)
{

  if (!ubx_send_valset(gps, layer, items, item_count))
  {

    return false;
  }

  while (1)
  {

    ubx_cmd_state_t state = ubx_get_cmd_state(&gps->ubx_cmd_handler, timeout_ms);

    if (state == UBX_CMD_STATE_ACK)
    {

      return true;
    }

    if (state == UBX_CMD_STATE_NAK)
    {

      return false;
    }

    if (state == UBX_CMD_STATE_TIMEOUT)
    {

      return false;
    }

    // 10ms 대기

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/**

 * @brief UBX VAL-GET 전송

 *

 * @param[inout] gps GPS 구조체

 * @param[in] layer Layer (RAM/BBR/Flash)

 * @param[in] key_ids Key ID 배열

 * @param[in] key_count Key 개수

 * @return true 전송 성공, false 실패

 */

bool ubx_send_valget(gps_t *gps, ubx_cfg_layer_t layer,

                     const uint32_t *key_ids, size_t key_count)
{

  ubx_cmd_handler_t *handler = &gps->ubx_cmd_handler;

  // 이미 대기 중인 명령이 있으면 실패

  if (handler->state == UBX_CMD_STATE_WAITING)
  {

    return false;
  }

  uint8_t msg[256];

  size_t offset = 0;

  // Sync bytes

  msg[offset++] = UBX_SYNC_1;

  msg[offset++] = UBX_SYNC_2;

  // Class & ID

  msg[offset++] = GPS_UBX_CLASS_CFG;

  msg[offset++] = GPS_UBX_CFG_ID_VALGET;

  // Payload length

  size_t payload_start = offset;

  offset += 2;

  size_t payload_offset = offset;

  // VAL-GET 헤더

  msg[offset++] = 0x00; // Version

  msg[offset++] = (uint8_t)layer; // Layer

  msg[offset++] = 0x00; // Position (reserved)

  msg[offset++] = 0x00; // Position (reserved)

  // Key IDs 추가

  for (size_t i = 0; i < key_count; i++)
  {

    msg[offset++] = (key_ids[i] >> 0) & 0xFF;

    msg[offset++] = (key_ids[i] >> 8) & 0xFF;

    msg[offset++] = (key_ids[i] >> 16) & 0xFF;

    msg[offset++] = (key_ids[i] >> 24) & 0xFF;
  }

  // Payload length 설정

  uint16_t payload_len = offset - payload_offset;

  msg[payload_start] = payload_len & 0xFF;

  msg[payload_start + 1] = (payload_len >> 8) & 0xFF;

  // Checksum 계산

  uint8_t ck_a, ck_b;

  ubx_calc_checksum(&msg[2], offset - 2, &ck_a, &ck_b);

  msg[offset++] = ck_a;

  msg[offset++] = ck_b;

  // Pending 설정

  handler->pending_cls = GPS_UBX_CLASS_CFG;

  handler->pending_id = GPS_UBX_CFG_ID_VALGET;

  handler->state = UBX_CMD_STATE_WAITING;

  handler->timestamp = get_tick_ms();

  // UART로 전송

  gps->ops->send((const char *)msg, offset);

  return true;
}

/**

 * @brief 파싱한 ubx ack 프로토콜 처리

 *

 * @param[inout] gps

 */

static void store_ubx_ack_data(gps_t *gps)
{

  // ACK/NAK payload: [clsID, msgID]

  if (gps->ubx.len == 2)
  {

    uint8_t acked_cls = gps->payload[4]; // Payload 시작

    uint8_t acked_id = gps->payload[5];

    bool is_ack = (gps->ubx.id == GPS_UBX_ACK_ID_ACK);

    // ACK/NAK 처리

    handle_ubx_ack(gps, acked_cls, acked_id, is_ack);
  }
}

/**

 * @brief 비동기 초기화 컨텍스트 초기화

 *

 * @param[inout] ctx 초기화 컨텍스트

 */

void ubx_init_context_init(ubx_init_context_t *ctx)
{

  ctx->state = UBX_INIT_STATE_IDLE;

  ctx->current_step = 0;

  ctx->configs = NULL;

  ctx->config_count = 0;

  ctx->layer = UBX_CFG_LAYER_RAM;

  ctx->on_complete = NULL;

  ctx->user_data = NULL;

  ctx->retry_count = 0;

  ctx->max_retries = 3; // 기본 3번 재시도
}

/**

 * @brief 비동기 초기화 내부 콜백 (다음 단계 진행)

 *

 * @param[in] ack ACK 여부

 * @param[inout] user_data gps_t 포인터

 */

static void ubx_init_async_callback(bool ack, void *user_data)
{

  gps_t *gps = (gps_t *)user_data;

  ubx_init_context_t *ctx = &gps->ubx_init_ctx;

  if (!ack)
  {

    // NAK 받음 - 재시도

    ctx->retry_count++;

    if (ctx->retry_count >= ctx->max_retries)
    {

      // 재시도 횟수 초과 - 실패

      ctx->state = UBX_INIT_STATE_ERROR;

      if (ctx->on_complete)
      {

        ctx->on_complete(false, ctx->current_step, ctx->user_data);
      }

      return;
    }

    // 재시도 (ubx_init_async_process()에서 재전송)

    return;
  }

  // ACK 받음 - 다음 단계로

  ctx->retry_count = 0; // 재시도 카운터 리셋

  ctx->current_step++;

  if (ctx->current_step >= ctx->config_count)
  {

    // 모든 단계 완료

    ctx->state = UBX_INIT_STATE_DONE;

    if (ctx->on_complete)
    {

      ctx->on_complete(true, 0, ctx->user_data);
    }
  }

  // 다음 단계는 ubx_init_async_process()에서 전송
}

/**

 * @brief 비동기 초기화 시작

 *

 * @param[inout] gps GPS 구조체

 * @param[in] layer Layer (RAM/BBR/Flash)

 * @param[in] configs 설정 배열 (유지해야 함!)

 * @param[in] config_count 설정 개수

 * @param[in] on_complete 완료 콜백

 * @param[in] user_data 콜백 데이터

 * @return true 시작 성공, false 이미 실행 중

 */

bool ubx_init_async_start(gps_t *gps, ubx_cfg_layer_t layer,

                          const ubx_cfg_item_t *configs, size_t config_count,

                          ubx_init_complete_callback_t on_complete, void *user_data)
{

  ubx_init_context_t *ctx = &gps->ubx_init_ctx;

  // 이미 실행 중이면 실패

  if (ctx->state == UBX_INIT_STATE_RUNNING)
  {

    return false;
  }

  // 컨텍스트 설정

  ctx->state = UBX_INIT_STATE_RUNNING;

  ctx->current_step = 0;

  ctx->configs = configs;

  ctx->config_count = config_count;

  ctx->layer = layer;

  ctx->on_complete = on_complete;

  ctx->user_data = user_data;

  ctx->retry_count = 0;

  // 첫 번째 설정 전송

  if (config_count > 0)
  {

    if (!ubx_send_valset_cb(gps, layer, &configs[0], 1,

                            ubx_init_async_callback, gps))
    {

      // 전송 실패

      ctx->state = UBX_INIT_STATE_ERROR;

      if (on_complete)
      {

        on_complete(false, 0, user_data);
      }

      return false;
    }
  }

  return true;
}

/**

 * @brief 비동기 초기화 진행 (주기적으로 호출)

 *

 * @param[inout] gps GPS 구조체

 */

void ubx_init_async_process(gps_t *gps)
{

  ubx_init_context_t *ctx = &gps->ubx_init_ctx;

  // 실행 중이 아니면 스킵

  if (ctx->state != UBX_INIT_STATE_RUNNING)
  {

    return;
  }

  // 명령 상태 확인

  ubx_cmd_state_t cmd_state = ubx_get_cmd_state(&gps->ubx_cmd_handler, 3000);

  if (cmd_state == UBX_CMD_STATE_WAITING)
  {

    // 아직 대기 중

    return;
  }

  if (cmd_state == UBX_CMD_STATE_TIMEOUT)
  {

    // 타임아웃 - 재시도

    ctx->retry_count++;

    if (ctx->retry_count >= ctx->max_retries)
    {

      // 재시도 횟수 초과 - 실패

      ctx->state = UBX_INIT_STATE_ERROR;

      if (ctx->on_complete)
      {

        ctx->on_complete(false, ctx->current_step, ctx->user_data);
      }

      return;
    }

    // 재전송

    ubx_send_valset_cb(gps, ctx->layer, &ctx->configs[ctx->current_step], 1,

                       ubx_init_async_callback, gps);

    return;
  }

  // ACK 또는 NAK은 콜백에서 처리됨

  // current_step이 증가했으면 다음 설정 전송

  if (cmd_state == UBX_CMD_STATE_ACK &&

      ctx->current_step < ctx->config_count &&

      ctx->state == UBX_INIT_STATE_RUNNING)
  {

    // 다음 설정 전송

    if (!ubx_send_valset_cb(gps, ctx->layer, &ctx->configs[ctx->current_step], 1,

                            ubx_init_async_callback, gps))
    {

      // 전송 실패 (이미 대기 중인 명령 있음?)

      // 다음 process()에서 재시도
    }
  }
}

/**

 * @brief 비동기 초기화 상태 확인

 *

 * @param[in] gps GPS 구조체

 * @return ubx_init_state_t 현재 상태

 */

ubx_init_state_t ubx_init_async_get_state(gps_t *gps)
{
  return gps->ubx_init_ctx.state;
}

/**

 * @brief 비동기 초기화 취소

 *

 * @param[inout] gps GPS 구조체

 */

void ubx_init_async_cancel(gps_t *gps)
{

  ubx_init_context_t *ctx = &gps->ubx_init_ctx;

  ctx->state = UBX_INIT_STATE_IDLE;

  ctx->current_step = 0;
}


bool ubx_send_cfg_cfg(gps_t *gps, uint32_t clear_mask, uint32_t save_mask,
                      uint32_t load_mask, ubx_ack_callback_t callback, void *user_data)

{

  ubx_cmd_handler_t *handler = &gps->ubx_cmd_handler;

 

  // 이미 대기 중인 명령이 있으면 실패

  if (handler->state == UBX_CMD_STATE_WAITING)

  {

    return false;

  }

 

  // UBX-CFG-CFG 메시지 생성

  uint8_t msg[256];

  size_t offset = 0;

 

  // Sync bytes

  msg[offset++] = UBX_SYNC_1;

  msg[offset++] = UBX_SYNC_2;

 

  // Class & ID

  msg[offset++] = GPS_UBX_CLASS_CFG;

  msg[offset++] = GPS_UBX_CFG_ID_CFG;

 

  // Payload length (13 bytes)

  msg[offset++] = 13;

  msg[offset++] = 0;

 

  size_t payload_start = offset;

 

  // clearMask (4 bytes, little-endian)

  msg[offset++] = (clear_mask >> 0) & 0xFF;

  msg[offset++] = (clear_mask >> 8) & 0xFF;

  msg[offset++] = (clear_mask >> 16) & 0xFF;

  msg[offset++] = (clear_mask >> 24) & 0xFF;

 

  // saveMask (4 bytes, little-endian)

  msg[offset++] = (save_mask >> 0) & 0xFF;

  msg[offset++] = (save_mask >> 8) & 0xFF;

  msg[offset++] = (save_mask >> 16) & 0xFF;

  msg[offset++] = (save_mask >> 24) & 0xFF;

 

  // loadMask (4 bytes, little-endian)

  msg[offset++] = (load_mask >> 0) & 0xFF;

  msg[offset++] = (load_mask >> 8) & 0xFF;

  msg[offset++] = (load_mask >> 16) & 0xFF;

  msg[offset++] = (load_mask >> 24) & 0xFF;

 

  // deviceMask (1 byte) - optional but included

  // 0x17 = BBR + Flash + EEPROM

  msg[offset++] = 0x17;

 

  // Checksum 계산

  uint8_t ck_a, ck_b;

  ubx_calc_checksum(&msg[2], offset - 2, &ck_a, &ck_b);

  msg[offset++] = ck_a;

  msg[offset++] = ck_b;

 

  // Pending 설정

  handler->pending_cls = GPS_UBX_CLASS_CFG;

  handler->pending_id = GPS_UBX_CFG_ID_CFG;

  handler->state = UBX_CMD_STATE_WAITING;

  handler->timestamp = get_tick_ms();

 

  // 콜백 등록

  handler->callback = callback;

  handler->callback_data = user_data;

 

  // UART로 전송

  gps->ops->send((const char *)msg, offset);

 

  return true;

}