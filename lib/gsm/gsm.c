#include "gsm.h"
#include "parser.h" // parser.c 함수 사용
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_usart.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef TAG
  #define TAG "GSM"
#endif

#include "log.h"

#define IS_ASCII(x) (((x) >= 32 && (x) <= 126) || (x) == '\r' || (x) == '\n')

void handle_urc_qcfg(gsm_t *gsm, const char *data, size_t len);

void handle_urc_rdy(gsm_t *gsm, const char *data, size_t len)
{
  gsm->status.is_powerd = 1;

  if (gsm->evt_handler.handler)
  {
    gsm->evt_handler.handler(GSM_EVT_RDY, gsm->evt_handler.args);
  }
}

void handle_urc_powered_down(gsm_t *gsm, const char *data, size_t len)
{
  gsm->status.is_powerd = 0;

  if (gsm->evt_handler.handler)
  {
    gsm->evt_handler.handler(GSM_EVT_POWERED_DOWN, gsm->evt_handler.args);
  }
}

void handle_urc_cmee(gsm_t *gsm, const char *data, size_t len)
{

}

void handle_urc_cgdcont(gsm_t *gsm, const char *data, size_t len)
{
  gsm_msg_t *target;
  gsm_at_mode_t mode;
  bool is_urc;
  gsm_msg_t local_msg = {0};

  if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_CGDCONT)
  {
    target = &gsm->current_cmd->msg;
    mode = gsm->current_cmd->at_mode;
    is_urc = false;
  }
  else
  {
    target = &local_msg;
    mode = GSM_AT_READ;
    is_urc = true;
  }

  if (mode == GSM_AT_READ) {
    // +CGDCONT: 1,"IP","internet"
    const char *p = data;
    size_t idx = target->cgdcont.count;
    if (idx < 5)
    {
      gsm_pdp_context_t *ctx = &target->cgdcont.contexts[idx];
      ctx->cid = parse_uint32(&p);
      parse_string_quoted(&p, (char *)&ctx->type, sizeof(ctx->type));
      parse_string_quoted(&p, ctx->apn, sizeof(ctx->apn));
      target->cgdcont.count++;
    }
  }

  if (is_urc && target->cgdcont.count > 0) {

  }
}

void handle_urc_cpin(gsm_t *gsm, const char *data, size_t len) {
  gsm_msg_t *target;
  gsm_at_mode_t mode;
  bool is_urc;
  gsm_msg_t local_msg = {0};

  if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_CPIN) {
    target = &gsm->current_cmd->msg;
    mode = gsm->current_cmd->at_mode;
    is_urc = false;
  } else {
    target = &local_msg;
    mode = GSM_AT_READ;
    is_urc = true;
  }

  if (mode == GSM_AT_READ) {
    // +CPIN: READY
    const char *p = data;
    parse_string(&p, target->cpin.code, sizeof(target->cpin.code));
  }

  if (is_urc) {

  }
}

void handle_urc_cops(gsm_t *gsm, const char *data, size_t len) {
  gsm_msg_t *target;
  gsm_at_mode_t mode;
  bool is_urc;
  gsm_msg_t local_msg = {0};

  if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_COPS) {
    target = &gsm->current_cmd->msg;
    mode = gsm->current_cmd->at_mode;
    is_urc = false;
  } else {
    target = &local_msg;
    mode = GSM_AT_READ;
    is_urc = true;
  }

  if (mode == GSM_AT_READ) {
    const char *p = data;
    target->cops.mode = parse_uint32(&p);
    target->cops.format = parse_uint32(&p);
    parse_string_quoted(&p, target->cops.oper, sizeof(target->cops.oper));
    target->cops.act = parse_uint32(&p);
  }

  if (is_urc && target->cops.oper[0]) {

  }
}

/**
 * @brief +QIOPEN URC 핸들러
 * 형식: +QIOPEN: <connectID>,<err>
 * 예: +QIOPEN: 0,0  (연결 성공)
 */
void handle_urc_qiopen(gsm_t *gsm, const char *data, size_t len) {
  gsm_msg_t *target;
  bool is_urc;
  gsm_msg_t local_msg = {0};

  if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_QIOPEN) {
    target = &gsm->current_cmd->msg;
    is_urc = false;
  } else {
    target = &local_msg;
    is_urc = true;
  }

  // 파싱: <connectID>,<err>
  const char *p = data;
  target->qiopen.connect_id = parse_uint32(&p);
  target->qiopen.result = parse_int32(&p);

  // 소켓 상태 업데이트
  if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
    uint8_t cid = target->qiopen.connect_id;
    if (cid < GSM_TCP_MAX_SOCKETS) {
      gsm_tcp_socket_t *socket = &gsm->tcp.sockets[cid];
      if (target->qiopen.result == 0) {
        // 연결 성공
        socket->state = GSM_TCP_STATE_CONNECTED;

        if (is_urc && gsm->evt_handler.handler) {
          gsm->evt_handler.handler(GSM_EVT_TCP_CONNECTED, &cid);
        }
      } else {
        // 연결 실패
        socket->state = GSM_TCP_STATE_CLOSED;
      }

      if (socket->open_sem) {
        xSemaphoreGive(socket->open_sem);
      }
    }
    xSemaphoreGive(gsm->tcp.tcp_mutex);
  }
}

/**
 * @brief +QICLOSE URC 핸들러
 * 형식: +QICLOSE: <connectID>
 */
void handle_urc_qiclose(gsm_t *gsm, const char *data, size_t len) {
  gsm_msg_t *target;
  bool is_urc;
  gsm_msg_t local_msg = {0};

  if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_QICLOSE) {
    target = &gsm->current_cmd->msg;
    is_urc = false;
  } else {
    target = &local_msg;
    is_urc = true;
  }

  // 파싱: <connectID>
  const char *p = data;
  target->qiclose.connect_id = parse_uint32(&p);

  // 소켓 상태 업데이트
  if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
    uint8_t cid = target->qiclose.connect_id;
    if (cid < GSM_TCP_MAX_SOCKETS) {
      gsm_tcp_socket_t *socket = &gsm->tcp.sockets[cid];

      // 연결 종료 콜백 저장
      tcp_close_callback_t on_close = socket->on_close;
      SemaphoreHandle_t close_sem = socket->close_sem;

      socket->state = GSM_TCP_STATE_CLOSED;
      socket->on_recv = NULL;
      socket->on_close = NULL;

      xSemaphoreGive(gsm->tcp.tcp_mutex);

      if (close_sem) {
        xSemaphoreGive(close_sem);
      }

      if (on_close) {
        on_close(cid);
      }

      if (is_urc && gsm->evt_handler.handler) {
        gsm->evt_handler.handler(GSM_EVT_TCP_CLOSED, &cid);
      }
      return;
    }
    xSemaphoreGive(gsm->tcp.tcp_mutex);
  }
}

/**
 * @brief +QISEND URC 핸들러
 * 형식: +QISEND: <connectID>,<sent_length>,<acked_length>
 */
void handle_urc_qisend(gsm_t *gsm, const char *data, size_t len) {
  gsm_msg_t *target;
  bool is_urc;
  gsm_msg_t local_msg = {0};

  if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_QISEND) {
    target = &gsm->current_cmd->msg;
    is_urc = false;
  } else {
    target = &local_msg;
    is_urc = true;
  }

  // 파싱: <connectID>,<sent_length>,<acked_length>
  const char *p = data;
  target->qisend.connect_id = parse_uint32(&p);
  target->qisend.sent_length = parse_uint32(&p);
  target->qisend.acked_length = parse_uint32(&p);

  if (is_urc && gsm->evt_handler.handler) {
    gsm->evt_handler.handler(GSM_EVT_TCP_SEND_OK, &target->qisend.connect_id);
  }
}

/**
 * @brief +QIRD URC 핸들러
 * 형식: +QIRD: <read_actual_length>
 * 다음 줄에 바이너리 데이터가 옴
 */
void handle_urc_qird(gsm_t *gsm, const char *data, size_t len) {
  gsm_msg_t *target;
  gsm_msg_t local_msg = {0};
  uint8_t connect_id = 0;

  if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_QIRD) {
    target = &gsm->current_cmd->msg;

    // params에서 connect_id 추출: "0,1460" → connect_id=0
    const char *param_p = gsm->current_cmd->params;
    connect_id = parse_uint32(&param_p);
  } else {
    target = &local_msg;
  }

  // 파싱: <read_actual_length>
  const char *p = data;
  target->qird.read_actual_length = parse_uint32(&p);
  target->qird.connect_id = connect_id;

  gsm->tcp.buffer.is_reading_data = true;
  gsm->tcp.buffer.expected_data_len = target->qird.read_actual_length;
  gsm->tcp.buffer.read_data_len = 0;
  gsm->tcp.buffer.rx_len = 0;
  gsm->tcp.buffer.current_connect_id = connect_id;
}

/**
 * @brief +QISTATE URC 핸들러
 * 형식: +QISTATE:
 * <connectID>,"<service_type>","<IP_address>",<remote_port>,<local_port>,<socket_state>,<contextID>,<serverID>,<access_mode>,"<AT_port>"
 * 예: +QISTATE: 0,"TCP","192.0.2.2",8009,65514,2,1,0,0,"usbmodem"
 */

void handle_urc_qistate(gsm_t *gsm, const char *data, size_t len) {
  gsm_msg_t *target;
  bool is_urc;
  gsm_msg_t local_msg = {0};

  // AT 응답인지 URC인지 판단
  if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_QISTATE) {
    target = &gsm->current_cmd->msg;
    is_urc = false;
  } else {
    target = &local_msg;
    is_urc = true;
  }

  // 파싱:
  // <connectID>,"<service_type>","<IP_address>",<remote_port>,<local_port>,<socket_state>,<contextID>,<serverID>,<access_mode>,"<AT_port>"
  const char *p = data;
  target->qistate.connect_id = parse_uint32(&p);
  parse_string_quoted(&p, target->qistate.service_type,
                      sizeof(target->qistate.service_type));
  parse_string_quoted(&p, target->qistate.remote_ip,
                      sizeof(target->qistate.remote_ip));
  target->qistate.remote_port = parse_uint32(&p);
  target->qistate.local_port = parse_uint32(&p);
  target->qistate.socket_state = parse_uint32(&p);
  target->qistate.context_id = parse_uint32(&p);
  target->qistate.server_id = parse_uint32(&p);
  target->qistate.access_mode = parse_uint32(&p);
  parse_string_quoted(&p, target->qistate.at_port,
                      sizeof(target->qistate.at_port));

  (void)is_urc;
}

/**
 * @brief +QIURC URC 핸들러
 * 형식:
 * - +QIURC: "recv",<connectID>  (데이터 수신 알림)
 * - +QIURC: "closed",<connectID>  (연결 종료 알림)
 */
void handle_urc_qiurc(gsm_t *gsm, const char *data, size_t len) {
  const char *p = data;
  char type[16] = {0};

  parse_string_quoted(&p, type, sizeof(type));

  if (strlen(type) == 0) {
    LOG_ERR("+QIURC 파싱 실패: type이 비어있음 (data=\"%.*s\")", (int)len,
            data);

    return;
  }

  if (strcmp(type, "recv") == 0) {
    uint8_t connect_id = parse_uint32(&p);

    if (connect_id < GSM_TCP_MAX_SOCKETS) {
      tcp_event_t evt = {.type = TCP_EVT_RECV_NOTIFY, .connect_id = connect_id};
      if (xQueueSend(gsm->tcp.event_queue, &evt, pdMS_TO_TICKS(10)) != pdTRUE) {
        LOG_ERR("+QIURC: \"recv\" 이벤트 큐 오버플로우! (connect_id=%d)",
                connect_id);
      } else {

      }

      if (gsm->evt_handler.handler) {
        gsm->evt_handler.handler(GSM_EVT_TCP_DATA_RECV, &connect_id);
      }
    } else {
      LOG_ERR("+QIURC: \"recv\" 잘못된 connect_id=%d (최대=%d)", connect_id,
              GSM_TCP_MAX_SOCKETS);
    }
  } else if (strcmp(type, "closed") == 0) {
    uint8_t connect_id = parse_uint32(&p);

    LOG_DEBUG("+QIURC: \"closed\",%d - 이벤트 큐에 추가", connect_id);

    if (connect_id < GSM_TCP_MAX_SOCKETS) {
      tcp_event_t evt = {
          .type = TCP_EVT_CLOSED_NOTIFY,
          .connect_id = connect_id};

      if (xQueueSend(gsm->tcp.event_queue, &evt, pdMS_TO_TICKS(10)) != pdTRUE) {

        LOG_ERR("+QIURC: \"closed\" 이벤트 큐 오버플로우! (connect_id=%d)",
                connect_id);
      } else {

      }

      if (gsm->evt_handler.handler) {
        gsm->evt_handler.handler(GSM_EVT_TCP_CLOSED, &connect_id);
      }
    } else {
      LOG_ERR("+QIURC: \"closed\" 잘못된 connect_id=%d (최대=%d)", connect_id,
              GSM_TCP_MAX_SOCKETS);
    }
  } else if (strcmp(type, "pdpdeact") == 0) {
    uint8_t context_id = parse_uint32(&p);
    LOG_WARN("+QIURC: \"pdpdeact\",%d - PDP context 비활성화", context_id);

    if (gsm->evt_handler.handler) {
      gsm->evt_handler.handler(GSM_EVT_PDP_DEACT, &context_id);
    }
  } else {
    LOG_ERR("+QIURC 알 수 없는 type=\"%s\" (data=\"%.*s\")", type, (int)len,
            data);
  }
}

void handle_urc_qcfg(gsm_t *gsm, const char *data, size_t len) {

  // QCFG 응답은 READ 모드에서만 처리

  if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_QCFG) {

    // 현재는 airplanecontrol만 지원하므로 별도 파싱 불필요

    // 필요시 파싱 로직 추가

  }

}

const urc_handler_entry_t urc_status_handlers[] = {
    {"RDY", handle_urc_rdy},
    {"POWERED DOWN", handle_urc_powered_down},
    {NULL, NULL}};

const urc_handler_entry_t urc_info_handlers[] = {
    {"+CMEE: ", handle_urc_cmee},       ///< 2.23
    {"+CGDCONT: ", handle_urc_cgdcont}, ///< 10.2
    {"+CPIN: ", handle_urc_cpin},       ///< 5.3
    {"+COPS: ", handle_urc_cops},       ///< 6.1
    {"+QIOPEN: ", handle_urc_qiopen},   ///< 2.3.6
    {"+QICLOSE: ", handle_urc_qiclose}, ///< 2.3.7
    {"+QISEND: ", handle_urc_qisend},   ///< 2.3.9
    {"+QIRD: ", handle_urc_qird},       ///< 2.3.10
    {"+QISTATE: ", handle_urc_qistate}, ///< 2.3.8
    {"+QIURC: ", handle_urc_qiurc},     
    {"+QCFG: ", handle_urc_qcfg}, 
    {NULL, NULL}};

const gsm_at_cmd_entry_t gsm_at_cmd_handlers[] = {
    {GSM_CMD_NONE, NULL, NULL, 0},

    /* 3GPP 명령어 */
    {GSM_CMD_AT, "AT", NULL, 300},
    {GSM_CMD_ATE, "ATE", NULL, 300},
    {GSM_CMD_CMEE, "AT+CMEE", "+CMEE: ", 300},
    {GSM_CMD_CGDCONT, "AT+CGDCONT", "+CGDCONT: ", 300},
    {GSM_CMD_CPIN, "AT+CPIN", "+CPIN: ", 5000},
    {GSM_CMD_COPS, "AT+COPS", "+COPS: ", 180000},
    {GSM_CMD_QPOWD, "AT+QPOWD", NULL, 40000},

    /* EC25 TCP 명령어 */
    {GSM_CMD_QIOPEN, "AT+QIOPEN", "+QIOPEN: ", 150000},
    {GSM_CMD_QICLOSE, "AT+QICLOSE", "+QICLOSE: ", 10000},
    {GSM_CMD_QISEND, "AT+QISEND", "+QISEND: ", 5000},
    {GSM_CMD_QIRD, "AT+QIRD", "+QIRD: ", 5000},
    {GSM_CMD_QISDE, "AT+QISDE", "+QISDE: ", 300},
    {GSM_CMD_QISTATE, "AT+QISTATE", "+QISTATE: ", 300},

    {GSM_CMD_QICFG, "AT+QICFG", "+QICFG: ", 300},
    {GSM_CMD_QCFG, "AT+QCFG", "+QCFG: ", 300},

    {GSM_CMD_NONE, NULL, NULL, 0}};

static inline void add_payload(gsm_t *gsm, char ch) {
  if (gsm->recv.len < GSM_PAYLOAD_SIZE - 1) {
    gsm->recv.data[gsm->recv.len] = ch;
    gsm->recv.data[++gsm->recv.len] = '\0';
  }
}

static inline void clear_payload(gsm_t *gsm) {
  gsm->recv.len = 0;
  gsm->recv.data[0] = '\0';
}

static inline size_t recv_payload_len(gsm_t *gsm) { return gsm->recv.len; }

static void handle_urc_status(gsm_t *gsm) {
  const urc_handler_entry_t *entry = gsm->urc_stat_tbl;

  while (entry->prefix != NULL) {
    const char *prefix = entry->prefix;
    size_t prefix_len = strlen(entry->prefix);

    if (strncmp(gsm->recv.data, prefix, prefix_len) == 0) {
      if (entry->handler) {
        entry->handler(gsm, NULL, 0);
      }
      return;
    }
    entry++;
  }
}

/**
 * @brief URC 및 AT 응답 처리
 *
 */
static void handle_urc_info(gsm_t *gsm) {
  const urc_handler_entry_t *entry = gsm->urc_info_tbl;

  LOG_DEBUG("handle_urc_info: recv.data=\"%s\" (len=%d)", gsm->recv.data,
            gsm->recv.len);

  while (entry->prefix != NULL) {
    const char *resp = entry->prefix;
    size_t resp_len = strlen(entry->prefix);

    if (strncmp(gsm->recv.data, resp, resp_len) == 0)
    {
      if (entry->handler)
      {
        const char *data_start = &gsm->recv.data[resp_len];
        size_t data_len =
            ((gsm->recv.len > resp_len) ? gsm->recv.len - resp_len : 0);


        if (gsm->cmd_mutex &&
            xSemaphoreTake(gsm->cmd_mutex, portMAX_DELAY) == pdTRUE)
        {
          entry->handler(gsm, data_start, data_len);
          xSemaphoreGive(gsm->cmd_mutex);
        }
        else
        {
          LOG_ERR("URC 핸들러 뮤텍스 획득 실패!");
        }
      }

      return;
    }

    entry++;
  }

  LOG_ERR("URC info 핸들러 매칭 실패: recv.data=\"%s\"", gsm->recv.data);
}

void gsm_parse_response(gsm_t *gsm) {
  char *line = gsm->recv.data;

  if (!strncmp(line, "OK", 2) || !strncmp(line, "SEND OK", 7)) {
    gsm->status.is_ok = 1;
    gsm->status.is_err = 0;

    if (xSemaphoreTake(gsm->cmd_mutex, portMAX_DELAY) == pdTRUE) {
      if (gsm->current_cmd) {
        SemaphoreHandle_t caller_sem = gsm->current_cmd->sem;
        at_cmd_handler callback = gsm->current_cmd->callback;
        gsm_cmd_t cmd_backup = gsm->current_cmd->cmd;
        tcp_pbuf_t *tx_pbuf = gsm->current_cmd->tx_pbuf;

        gsm_msg_t msg_backup;
        memcpy(&msg_backup, &gsm->current_cmd->msg, sizeof(msg_backup));

        gsm->current_cmd = NULL;

        if (gsm->producer_sem) {
          xSemaphoreGive(gsm->producer_sem);
        }

        xSemaphoreGive(gsm->cmd_mutex);

        if (caller_sem) {
          xSemaphoreGive(caller_sem);
        } else if (callback) {
          callback(gsm, cmd_backup, &msg_backup, true);
        }

        if (tx_pbuf) {
          tcp_pbuf_free(tx_pbuf);
        }

        return;
      }
      xSemaphoreGive(gsm->cmd_mutex);
    }
  } else if (!strncmp(line, "ERROR", 5)) {
    gsm->status.is_ok = 0;
    gsm->status.is_err = 1;

    if (xSemaphoreTake(gsm->cmd_mutex, portMAX_DELAY) == pdTRUE) {
      if (gsm->current_cmd) {
        SemaphoreHandle_t caller_sem = gsm->current_cmd->sem;
        at_cmd_handler callback = gsm->current_cmd->callback;
        gsm_cmd_t cmd_backup = gsm->current_cmd->cmd;
        tcp_pbuf_t *tx_pbuf = gsm->current_cmd->tx_pbuf;

        gsm->current_cmd = NULL;

        if (gsm->producer_sem) {
          xSemaphoreGive(gsm->producer_sem);
        }

        xSemaphoreGive(gsm->cmd_mutex);

        if (caller_sem) {
          xSemaphoreGive(caller_sem);
        } else if (callback) {
          callback(gsm, cmd_backup, NULL, false);
        }

        if (tx_pbuf) {
          tcp_pbuf_free(tx_pbuf);
        }

        return;
      }
      xSemaphoreGive(gsm->cmd_mutex);
    }
  } else if (line[0] == '+') {
    handle_urc_info(gsm);
  } else {
    handle_urc_status(gsm);
  }
}

void gsm_parse_process(gsm_t *gsm, const void *data, size_t len) {
  const uint8_t *d = data;
  static char ch_prev1 = 0;

  for (; len > 0; ++d, --len) {
    if (gsm->tcp.buffer.is_reading_data) {
      if (gsm->tcp.buffer.read_data_len < gsm->tcp.buffer.expected_data_len &&
          gsm->tcp.buffer.rx_len < GSM_TCP_RX_BUFFER_SIZE) {
        gsm->tcp.buffer.rx_buf[gsm->tcp.buffer.rx_len++] = *d;
        gsm->tcp.buffer.read_data_len++;

        if (gsm->tcp.buffer.read_data_len >=
            gsm->tcp.buffer.expected_data_len) {
          if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
            if (gsm->cmd_mutex &&
                xSemaphoreTake(gsm->cmd_mutex, portMAX_DELAY) == pdTRUE) {
              if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_QIRD) {
                  gsm->current_cmd->msg.qird.data = gsm->tcp.buffer.rx_buf;
                  gsm->current_cmd->msg.qird.read_actual_length =
                  gsm->tcp.buffer.rx_len;
                  gsm->current_cmd->msg.qird.connect_id =
                  gsm->tcp.buffer.current_connect_id;
              }
              xSemaphoreGive(gsm->cmd_mutex);
            }

            gsm->tcp.buffer.is_reading_data = false;
            xSemaphoreGive(gsm->tcp.tcp_mutex);
          }
        }
        continue;
      } else {
        if (xSemaphoreTake(gsm->tcp.tcp_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          gsm->tcp.buffer.is_reading_data = false;
          xSemaphoreGive(gsm->tcp.tcp_mutex);
        }
      }
    }

    if (*d == '>' && recv_payload_len(gsm) == 0) {
      if (xSemaphoreTake(gsm->cmd_mutex, portMAX_DELAY) == pdTRUE) {
        if (gsm->current_cmd && gsm->current_cmd->cmd == GSM_CMD_QISEND &&
            gsm->current_cmd->wait_type == GSM_WAIT_PROMPT) {
          if (gsm->current_cmd->tx_pbuf) {
            tcp_pbuf_t *pbuf = gsm->current_cmd->tx_pbuf;
            gsm->ops->send((const char *)pbuf->payload, pbuf->len);
          }
          gsm->current_cmd->wait_type = GSM_WAIT_EXPECTED;
        }
        xSemaphoreGive(gsm->cmd_mutex);
      }
      continue;
    }

    if (*d != '\r' && *d != '\n') {
      if (IS_ASCII(*d)) {
        add_payload(gsm, *d);
      }
    }

    if (ch_prev1 == '\r' && *d == '\n') {
      if (recv_payload_len(gsm)) {
        gsm_parse_response(gsm);
        clear_payload(gsm);
      }
    }

    ch_prev1 = *d;
  }
}

/**
 * @brief AT 커맨드 전송 (범용)
 *
 * @param gsm GSM 핸들
 * @param cmd 명령어 타입
 * @param at_mode 명령어 모드 (EXECUTE, READ, WRITE, TEST)
 * @param params 파라미터 문자열 (NULL이면 파라미터 없음)
 * @param callback 완료 콜백 (NULL이면 동기식)
 */
void gsm_send_at_cmd(gsm_t *gsm, gsm_cmd_t cmd, gsm_at_mode_t at_mode,
                     const char *params, at_cmd_handler callback) {
  gsm_at_cmd_t msg = {
      .at_mode = at_mode,
      .cmd = cmd,
      .wait_type = GSM_WAIT_NONE,
      .callback = NULL,
      .sem = NULL,
      .tx_pbuf = NULL,
  };

  if (params != NULL) {
    snprintf(msg.params, GSM_AT_CMD_PARAM_SIZE, "%s", params);
  } else {
    msg.params[0] = '\0';
  }

  if (callback) {
    msg.callback = callback;
    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);
  } else {
    gsm->status.is_ok = 0;
    gsm->status.is_err = 0;
    gsm->status.is_timeout = 0;

    msg.sem = xSemaphoreCreateBinary();
    SemaphoreHandle_t sem = msg.sem;

    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);

    uint32_t timeout_ms = gsm->at_tbl[cmd].timeout_ms;
    if (timeout_ms == 0)
      timeout_ms = 5000;
    timeout_ms += 1000;

    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xSemaphoreTake(sem, timeout_ticks);

    vSemaphoreDelete(sem);

    if (result != pdTRUE) {
      gsm->status.is_ok = 0;
      gsm->status.is_err = 0;
      gsm->status.is_timeout = 1;
    }
  }
}

void gsm_send_at_cmee(gsm_t *gsm, gsm_at_mode_t at_mode, gsm_cmee_mode_t cmee,
                      at_cmd_handler callback) {
  char params[8] = {0};

  if (at_mode == GSM_AT_WRITE) {
    snprintf(params, sizeof(params), "%d", cmee);
    gsm_send_at_cmd(gsm, GSM_CMD_CMEE, at_mode, params, callback);
  } else {
    gsm_send_at_cmd(gsm, GSM_CMD_CMEE, at_mode, NULL, callback);
  }
}

void gsm_send_at_cgdcont(gsm_t *gsm, gsm_at_mode_t at_mode,
                         gsm_pdp_context_t *ctx, at_cmd_handler callback) {
  gsm_at_cmd_t msg = {
      .at_mode = at_mode,
      .cmd = GSM_CMD_CGDCONT,
      .wait_type = GSM_WAIT_NONE,
      .callback = NULL,
      .sem = NULL,
      .tx_pbuf = NULL,
  };

  if (at_mode == GSM_AT_WRITE) {
    const char *pdp_type_str = "";

    switch (ctx->type) {
    case GSM_PDP_TYPE_IP:
      pdp_type_str = "IP";
      break;
    case GSM_PDP_TYPE_PPP:
      pdp_type_str = "PPP";
      break;
    case GSM_PDP_TYPE_IPV6:
      pdp_type_str = "IPV6";
      break;
    case GSM_PDP_TYPE_IPV4V6:
      pdp_type_str = "IPV4V6";
      break;
    default:
      break;
    }

    snprintf(msg.params, GSM_AT_CMD_PARAM_SIZE, "%d,\"%s\",\"%s\"", ctx->cid,
             pdp_type_str, ctx->apn);
  } else {
    msg.params[0] = '\0';
  }

  if (callback) {
    msg.callback = callback;
    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);
  } else {
    gsm->status.is_ok = 0;
    gsm->status.is_err = 0;
    gsm->status.is_timeout = 0;

    msg.sem = xSemaphoreCreateBinary();
    SemaphoreHandle_t sem = msg.sem;

    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);

    uint32_t timeout_ms = gsm->at_tbl[GSM_CMD_CGDCONT].timeout_ms;
    if (timeout_ms == 0)
      timeout_ms = 5000;
    timeout_ms += 1000;

    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xSemaphoreTake(sem, timeout_ticks);

    vSemaphoreDelete(sem);

    if (result != pdTRUE) {
      gsm->status.is_ok = 0;
      gsm->status.is_err = 0;
      gsm->status.is_timeout = 1;
    }
  }
}

int gsm_uart_send(const char *data, size_t len) {
  for (int i = 0; i < len; i++) {
    while (!LL_USART_IsActiveFlag_TXE(USART1))
      ;
    LL_USART_TransmitData8(USART1, *(data + i));
  }

  while (!LL_USART_IsActiveFlag_TC(USART1))
    ;

  return 0;
}

extern int gsm_port_reset(void);

static const gsm_hal_ops_t stm32_hal_ops = {.reset = gsm_port_reset,
                                            .send = gsm_uart_send};

void gsm_init(gsm_t *gsm, evt_handler_t handler, void *args) {
  memset(gsm, 0, sizeof(gsm_t));

  gsm->evt_handler.handler = handler;
  gsm->evt_handler.args = args;

  gsm->current_cmd = NULL;
  gsm->cmd_mutex = xSemaphoreCreateMutex();
  gsm->producer_sem =
      xSemaphoreCreateBinary();

  gsm->at_tbl = gsm_at_cmd_handlers;
  gsm->urc_stat_tbl = urc_status_handlers;
  gsm->urc_info_tbl = urc_info_handlers;

  gsm->ops = &stm32_hal_ops;
  gsm->at_cmd_queue = xQueueCreate(9, sizeof(gsm_at_cmd_t));

  gsm_tcp_init(gsm);
}

tcp_pbuf_t *tcp_pbuf_alloc(size_t len) {
  tcp_pbuf_t *pbuf = (tcp_pbuf_t *)pvPortMalloc(sizeof(tcp_pbuf_t));
  if (!pbuf)
    return NULL;

  pbuf->payload = (uint8_t *)pvPortMalloc(len);
  if (!pbuf->payload) {
    vPortFree(pbuf);
    return NULL;
  }

  pbuf->len = len;
  pbuf->tot_len = len;
  pbuf->next = NULL;

  return pbuf;
}

void tcp_pbuf_free(tcp_pbuf_t *pbuf) {
  if (!pbuf)
    return;

  if (pbuf->payload) {
    vPortFree(pbuf->payload);
  }
  vPortFree(pbuf);
}

void tcp_pbuf_free_chain(tcp_pbuf_t *pbuf) {
  tcp_pbuf_t *next;

  while (pbuf) {
    next = pbuf->next;
    tcp_pbuf_free(pbuf);
    pbuf = next;
  }
}

int tcp_pbuf_enqueue(gsm_tcp_socket_t *socket, tcp_pbuf_t *pbuf) {
  if (!socket || !pbuf)
    return -1;

  while (socket->pbuf_total_len + pbuf->len > GSM_TCP_PBUF_MAX_LEN) {
    tcp_pbuf_t *old = tcp_pbuf_dequeue(socket);
    if (old) {
      tcp_pbuf_free(old);
    } else {

      return -1;
    }
  }

  pbuf->next = NULL;

  if (!socket->pbuf_head) {
    socket->pbuf_head = pbuf;
    socket->pbuf_tail = pbuf;
  } else {
    socket->pbuf_tail->next = pbuf;
    socket->pbuf_tail = pbuf;
  }

  socket->pbuf_total_len += pbuf->len;
  return 0;
}

tcp_pbuf_t *tcp_pbuf_dequeue(gsm_tcp_socket_t *socket) {
  if (!socket || !socket->pbuf_head)
    return NULL;

  tcp_pbuf_t *pbuf = socket->pbuf_head;
  socket->pbuf_head = pbuf->next;

  if (!socket->pbuf_head) {
    socket->pbuf_tail = NULL;
  }

  socket->pbuf_total_len -= pbuf->len;
  pbuf->next = NULL;

  return pbuf;
}

static void tcp_read_complete_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                       bool is_ok) {
  if (!is_ok || !msg || cmd != GSM_CMD_QIRD)
    return;

  gsm_msg_t *m = (gsm_msg_t *)msg;

  if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
    uint8_t cid = m->qird.connect_id;

    if (cid < GSM_TCP_MAX_SOCKETS && m->qird.read_actual_length > 0) {
      gsm_tcp_socket_t *socket = &gsm->tcp.sockets[cid];

      // pbuf 할당 및 데이터 복사
      tcp_pbuf_t *pbuf = tcp_pbuf_alloc(m->qird.read_actual_length);
      if (pbuf) {
        memcpy(pbuf->payload, m->qird.data, m->qird.read_actual_length);

        tcp_pbuf_enqueue(socket, pbuf);

        tcp_recv_callback_t on_recv = socket->on_recv;

        xSemaphoreGive(gsm->tcp.tcp_mutex);

        if (on_recv) {
          on_recv(cid);
        }
        tcp_event_t evt = {.type = TCP_EVT_CONTINUE_READ, .connect_id = cid};
        xQueueSend(gsm->tcp.event_queue, &evt, 0);  // ⭐ 추가
        return;
      }
    }
    xSemaphoreGive(gsm->tcp.tcp_mutex);
  }
}


static void gsm_tcp_task(void *arg) {
  gsm_t *gsm = (gsm_t *)arg;
  tcp_event_t evt;

  while (1) {
    if (xQueueReceive(gsm->tcp.event_queue, &evt, portMAX_DELAY) == pdTRUE) {
      switch (evt.type) {
      case TCP_EVT_RECV_NOTIFY: {
        if (evt.connect_id < GSM_TCP_MAX_SOCKETS) {
          gsm_tcp_read(gsm, evt.connect_id, 1460, tcp_read_complete_callback);
        }
        break;
      }

      case TCP_EVT_CLOSED_NOTIFY: {
        if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
          if (evt.connect_id < GSM_TCP_MAX_SOCKETS) {
            gsm_tcp_socket_t *socket = &gsm->tcp.sockets[evt.connect_id];

            // pbuf 정리
            tcp_pbuf_free_chain(socket->pbuf_head);
            socket->pbuf_head = NULL;
            socket->pbuf_tail = NULL;
            socket->pbuf_total_len = 0;

            // 소켓 초기화
            socket->state = GSM_TCP_STATE_CLOSED;
            tcp_close_callback_t on_close = socket->on_close;
            socket->on_recv = NULL;
            socket->on_close = NULL;

            xSemaphoreGive(gsm->tcp.tcp_mutex);

            if (on_close) {
              on_close(evt.connect_id);
            }
          } else {
            xSemaphoreGive(gsm->tcp.tcp_mutex);
          }
        }
        break;
      }

      case TCP_EVT_CONTINUE_READ: {
        if (evt.connect_id < GSM_TCP_MAX_SOCKETS) {
          gsm_tcp_read(gsm, evt.connect_id, 1460, tcp_read_complete_callback);
        }
        break;
      }

      default:
        break;
      }
    }
  }
}

void gsm_tcp_init(gsm_t *gsm) {
  if (!gsm)
    return;

  gsm->tcp.tcp_mutex = xSemaphoreCreateMutex();

  for (int i = 0; i < GSM_TCP_MAX_SOCKETS; i++) {
    gsm->tcp.sockets[i].connect_id = i;
    gsm->tcp.sockets[i].state = GSM_TCP_STATE_CLOSED;
    gsm->tcp.sockets[i].remote_ip[0] = '\0';
    gsm->tcp.sockets[i].remote_port = 0;
    gsm->tcp.sockets[i].local_port = 0;
    gsm->tcp.sockets[i].pbuf_head = NULL;
    gsm->tcp.sockets[i].pbuf_tail = NULL;
    gsm->tcp.sockets[i].pbuf_total_len = 0;
    gsm->tcp.sockets[i].on_recv = NULL;
    gsm->tcp.sockets[i].on_close = NULL;
  }

  memset(&gsm->tcp.buffer, 0, sizeof(gsm_tcp_buffer_t));

  gsm->tcp.event_queue = xQueueCreate(10, sizeof(tcp_event_t));

  xTaskCreate(gsm_tcp_task, "gsm_tcp", 1536, gsm, tskIDLE_PRIORITY + 3,
              &gsm->tcp.task_handle);
}

gsm_tcp_socket_t *gsm_tcp_get_socket(gsm_t *gsm, uint8_t connect_id) {
  if (!gsm || connect_id >= GSM_TCP_MAX_SOCKETS) {
    return NULL;
  }

  return &gsm->tcp.sockets[connect_id];
}

int gsm_tcp_open(gsm_t *gsm, uint8_t connect_id, uint8_t context_id,
                 const char *remote_ip, uint16_t remote_port,
                 uint16_t local_port, tcp_recv_callback_t on_recv,
                 tcp_close_callback_t on_close, at_cmd_handler callback) {
  if (!gsm || connect_id >= GSM_TCP_MAX_SOCKETS || !remote_ip) {
    return -1;
  }

  gsm_tcp_socket_t *socket = &gsm->tcp.sockets[connect_id];

  if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
    if (socket->state != GSM_TCP_STATE_CLOSED) {
      xSemaphoreGive(gsm->tcp.tcp_mutex);
      return -1;
    }

    // 소켓 설정
    socket->state = GSM_TCP_STATE_OPENING;
    strncpy(socket->remote_ip, remote_ip, sizeof(socket->remote_ip) - 1);
    socket->remote_port = remote_port;
    socket->local_port = local_port;
    socket->on_recv = on_recv;
    socket->on_close = on_close;

    socket->open_sem = xSemaphoreCreateBinary();

    xSemaphoreGive(gsm->tcp.tcp_mutex);
  }

  gsm_at_cmd_t msg = {
      .at_mode = GSM_AT_WRITE,
      .cmd = GSM_CMD_QIOPEN,
      .wait_type = GSM_WAIT_EXPECTED,
      .callback = callback,
      .sem = NULL,
      .tx_pbuf = NULL,
  };

  snprintf(msg.params, GSM_AT_CMD_PARAM_SIZE, "%d,%d,\"TCP\",\"%s\",%d,%d,0",
           context_id, connect_id, remote_ip, remote_port, local_port);

  if (callback) {
    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);
    return 0;
  } else {
    gsm->status.is_ok = 0;
    gsm->status.is_err = 0;
    gsm->status.is_timeout = 0;

    msg.sem = xSemaphoreCreateBinary();
    SemaphoreHandle_t sem = msg.sem;

    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);

    uint32_t timeout_ms = gsm->at_tbl[GSM_CMD_QIOPEN].timeout_ms;
    if (timeout_ms == 0)
      timeout_ms = 150000;
    timeout_ms += 1000;

    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xSemaphoreTake(sem, timeout_ticks);

    vSemaphoreDelete(sem);

    if (result != pdTRUE || !gsm->status.is_ok) {
      if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
        socket->state = GSM_TCP_STATE_CLOSED;
        if (socket->open_sem) {
          vSemaphoreDelete(socket->open_sem);
          socket->open_sem = NULL;
        }
        xSemaphoreGive(gsm->tcp.tcp_mutex);
      }
      return -1;
    }

    result = xSemaphoreTake(socket->open_sem, timeout_ticks);
    if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
      if (socket->open_sem) {
        vSemaphoreDelete(socket->open_sem);
        socket->open_sem = NULL;
      }

      xSemaphoreGive(gsm->tcp.tcp_mutex);
    }

    if (result != pdTRUE || socket->state != GSM_TCP_STATE_CONNECTED) {
    }

    return 0;
  }
}

int gsm_tcp_close(gsm_t *gsm, uint8_t connect_id, at_cmd_handler callback) {
  if (!gsm || connect_id >= GSM_TCP_MAX_SOCKETS) {
    return -1;
  }

  gsm_tcp_socket_t *socket = &gsm->tcp.sockets[connect_id];

  if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
    if (socket->state == GSM_TCP_STATE_CLOSED) {
      xSemaphoreGive(gsm->tcp.tcp_mutex);
      return 0;
    }

    socket->state = GSM_TCP_STATE_CLOSING;

    if (socket->close_sem) {
      vSemaphoreDelete(socket->close_sem);
    }

    socket->close_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(gsm->tcp.tcp_mutex);
  }

  gsm_at_cmd_t msg = {
      .at_mode = GSM_AT_WRITE,
      .cmd = GSM_CMD_QICLOSE,
      .wait_type = GSM_WAIT_NONE,
      .callback = callback,
      .sem = NULL,
      .tx_pbuf = NULL,
  };

  snprintf(msg.params, GSM_AT_CMD_PARAM_SIZE, "%d,0", connect_id);

  if (callback) {
    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);
    return 0;
  } else {
    gsm->status.is_ok = 0;
    gsm->status.is_err = 0;
    gsm->status.is_timeout = 0;

    msg.sem = xSemaphoreCreateBinary();
    SemaphoreHandle_t sem = msg.sem;

    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);

    uint32_t timeout_ms = gsm->at_tbl[GSM_CMD_QICLOSE].timeout_ms;
    if (timeout_ms == 0)
      timeout_ms = 10000;

    timeout_ms += 1000;

    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xSemaphoreTake(sem, timeout_ticks);

    vSemaphoreDelete(sem);

    if (result != pdTRUE || !gsm->status.is_ok) {
      if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
        if (socket->close_sem) {
          vSemaphoreDelete(socket->close_sem);
          socket->close_sem = NULL;
        }

        xSemaphoreGive(gsm->tcp.tcp_mutex);
      }
      return -1;
    }

    result = xSemaphoreTake(socket->close_sem, pdMS_TO_TICKS(5000));

    if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
      if (socket->close_sem) {
        vSemaphoreDelete(socket->close_sem);
        socket->close_sem = NULL;
      }
      xSemaphoreGive(gsm->tcp.tcp_mutex);
    }

    if (result != pdTRUE) {
      LOG_WARN("gsm_tcp_close: +QICLOSE URC 타임아웃, 강제 닫힘 처리");
      if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
        socket->state = GSM_TCP_STATE_CLOSED;
        xSemaphoreGive(gsm->tcp.tcp_mutex);
      }
      return 0;
    }
  }

  return 0;
}

int gsm_tcp_close_force(gsm_t *gsm, uint8_t connect_id) {

  if (!gsm || connect_id >= GSM_TCP_MAX_SOCKETS) {

    return -1;
  }

  gsm_tcp_socket_t *socket = &gsm->tcp.sockets[connect_id];

  LOG_INFO("gsm_tcp_close_force: connect_id=%d 강제 닫기 시작", connect_id);

  if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
    socket->state = GSM_TCP_STATE_CLOSING;
    if (socket->close_sem) {
      vSemaphoreDelete(socket->close_sem);
    }

    socket->close_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(gsm->tcp.tcp_mutex);
  }

  gsm_at_cmd_t msg = {
      .at_mode = GSM_AT_WRITE,
      .cmd = GSM_CMD_QICLOSE,
      .wait_type = GSM_WAIT_NONE,
      .callback = NULL,
      .sem = NULL,
      .tx_pbuf = NULL,
  };

  snprintf(msg.params, GSM_AT_CMD_PARAM_SIZE, "%d,0", connect_id);
  gsm->status.is_ok = 0;
  gsm->status.is_err = 0;
  gsm->status.is_timeout = 0;
  msg.sem = xSemaphoreCreateBinary();
  SemaphoreHandle_t sem = msg.sem;
  xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);

  TickType_t timeout_ticks = pdMS_TO_TICKS(11000);
  BaseType_t result = xSemaphoreTake(sem, timeout_ticks);
  vSemaphoreDelete(sem);

  if (result != pdTRUE) {
    LOG_ERR("gsm_tcp_close_force: OK 타임아웃");

    if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
      if (socket->close_sem) {
        vSemaphoreDelete(socket->close_sem);
        socket->close_sem = NULL;
      }
      socket->state = GSM_TCP_STATE_CLOSED;
      xSemaphoreGive(gsm->tcp.tcp_mutex);
    }

    return -1;
  }

  result = xSemaphoreTake(socket->close_sem, pdMS_TO_TICKS(5000));

  if (xSemaphoreTake(gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
    if (socket->close_sem) {
      vSemaphoreDelete(socket->close_sem);
      socket->close_sem = NULL;
    }

    if (result != pdTRUE) {
      LOG_WARN("gsm_tcp_close_force: +QICLOSE URC 타임아웃, 강제 닫힘 처리");
      socket->state = GSM_TCP_STATE_CLOSED;
    }
    xSemaphoreGive(gsm->tcp.tcp_mutex);
  }

  LOG_INFO("gsm_tcp_close_force: 완료");

  return 0;
}

int gsm_tcp_send(gsm_t *gsm, uint8_t connect_id, const uint8_t *data,
                 size_t len, at_cmd_handler callback) {
  if (!gsm || connect_id >= GSM_TCP_MAX_SOCKETS || !data || len == 0 ||
      len > 1460) {
    return -1;
  }

  gsm_tcp_socket_t *socket = &gsm->tcp.sockets[connect_id];

  if (socket->state != GSM_TCP_STATE_CONNECTED) {
    return -1;
  }

  tcp_pbuf_t *tx_pbuf = tcp_pbuf_alloc(len);
  if (!tx_pbuf) {
    return -1;
  }

  memcpy(tx_pbuf->payload, data, len);

  gsm_at_cmd_t msg = {
      .at_mode = GSM_AT_WRITE,
      .cmd = GSM_CMD_QISEND,
      .wait_type = GSM_WAIT_PROMPT,
      .callback = callback,
      .sem = NULL,
      .tx_pbuf = tx_pbuf,
  };

  snprintf(msg.params, GSM_AT_CMD_PARAM_SIZE, "%d,%u", connect_id, len);

  if (callback) {
    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);
    return 0;
  } else {
    gsm->status.is_ok = 0;
    gsm->status.is_err = 0;
    gsm->status.is_timeout = 0;

    msg.sem = xSemaphoreCreateBinary();
    SemaphoreHandle_t sem = msg.sem;

    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);

    uint32_t timeout_ms = gsm->at_tbl[GSM_CMD_QISEND].timeout_ms;
    if (timeout_ms == 0)
      timeout_ms = 5000;
    timeout_ms += 1000;

    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xSemaphoreTake(sem, timeout_ticks);

    vSemaphoreDelete(sem);

    if (result != pdTRUE || !gsm->status.is_ok) {
      return -1;
    }

    return 0;
  }
}

int gsm_tcp_read(gsm_t *gsm, uint8_t connect_id, size_t max_len,
                 at_cmd_handler callback) {
  if (!gsm || connect_id >= GSM_TCP_MAX_SOCKETS || max_len == 0 ||
      max_len > 1500) {
    return -1;
  }

  gsm_tcp_socket_t *socket = &gsm->tcp.sockets[connect_id];

  if (socket->state != GSM_TCP_STATE_CONNECTED) {
    return -1;
  }

  gsm_at_cmd_t msg = {
      .at_mode = GSM_AT_WRITE,
      .cmd = GSM_CMD_QIRD,
      .wait_type = GSM_WAIT_EXPECTED,
      .callback = callback,
      .sem = NULL,
      .tx_pbuf = NULL,
  };

  snprintf(msg.params, GSM_AT_CMD_PARAM_SIZE, "%d,%u", connect_id, max_len);

  if (callback) {
    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);
    return 0;
  } else {
    gsm->status.is_ok = 0;
    gsm->status.is_err = 0;
    gsm->status.is_timeout = 0;

    msg.sem = xSemaphoreCreateBinary();
    SemaphoreHandle_t sem = msg.sem;

    xQueueSend(gsm->at_cmd_queue, &msg, portMAX_DELAY);

    uint32_t timeout_ms = gsm->at_tbl[GSM_CMD_QIRD].timeout_ms;
    if (timeout_ms == 0)
      timeout_ms = 5000;
    timeout_ms += 1000;

    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xSemaphoreTake(sem, timeout_ticks);

    vSemaphoreDelete(sem);

    if (result != pdTRUE || !gsm->status.is_ok) {
      return -1;
    }

    return 0;
  }
}

/**
 * @brief 
 * 
 */
void gsm_send_at_ate(gsm_t *gsm, uint8_t echo_on, at_cmd_handler callback) {
  char params[4] = {0};
  snprintf(params, sizeof(params), "%d", echo_on ? 1 : 0);
  gsm_send_at_cmd(gsm, GSM_CMD_ATE, GSM_AT_EXECUTE, params, callback);
}

/**
 * @brief AT+QISDE 전송 (소켓 데이터 에코 설정)
 */
void gsm_send_at_qisde(gsm_t *gsm, gsm_at_mode_t at_mode, uint8_t echo_on,
                       at_cmd_handler callback) {
  char params[4] = {0};

  if (at_mode == GSM_AT_WRITE) {
    snprintf(params, sizeof(params), "%d", echo_on ? 1 : 0);
    gsm_send_at_cmd(gsm, GSM_CMD_QISDE, at_mode, params, callback);
  } else {
    gsm_send_at_cmd(gsm, GSM_CMD_QISDE, at_mode, NULL, callback);
  }
}

/**
 * @brief AT+QISTATE 전송 (소켓 상태 조회)
 */
void gsm_send_at_qistate(gsm_t *gsm, uint8_t query_type, uint8_t connect_id,
                         at_cmd_handler callback) {
  char params[8] = {0};

  // AT+QISTATE=<query_type>,<contextID 또는 connectID>
  snprintf(params, sizeof(params), "%d,%d", query_type, connect_id);
  gsm_send_at_cmd(gsm, GSM_CMD_QISTATE, GSM_AT_WRITE, params, callback);
}

void gsm_send_at_qicfg_keepalive(gsm_t *gsm, uint8_t enable, uint16_t keepidle,
                                 uint16_t keepinterval, uint8_t keepcount,
                                 at_cmd_handler callback) {
  char params[64] = {0};
  // AT+QICFG="tcp/keepalive",<enable>,<idle_time>,<interval_time>,<probe_cnt>
  snprintf(params, sizeof(params), "\"tcp/keepalive\",%d,%d,%d,%d", enable,
           keepidle, keepinterval, keepcount);
  gsm_send_at_cmd(gsm, GSM_CMD_QICFG, GSM_AT_WRITE, params, callback);
}

void gsm_send_at_qpowd(gsm_t *gsm, uint8_t mode, at_cmd_handler callback) {
  char params[4] = {0};
  snprintf(params, sizeof(params), "%d", mode);
  gsm_send_at_cmd(gsm, GSM_CMD_QPOWD, GSM_AT_WRITE, params, callback);
}

void gsm_send_at_qcfg_airplanecontrol(gsm_t *gsm, uint8_t mode, at_cmd_handler callback) {

  char params[32] = {0};

  // AT+QCFG="airplanecontrol",<mode>

  snprintf(params, sizeof(params), "\"airplanecontrol\",%d", mode);

  gsm_send_at_cmd(gsm, GSM_CMD_QCFG, GSM_AT_WRITE, params, callback);

}