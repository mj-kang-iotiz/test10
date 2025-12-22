#include "tcp_socket.h"
#include <stdlib.h>
#include <string.h>

#ifndef TAG
  #define TAG "TCP_SOCKET"
#endif

#include "log.h"


struct tcp_socket_s {
  gsm_t *gsm;
  uint8_t connect_id;
  bool is_connected;
  bool is_closed_by_peer;

  QueueHandle_t rx_queue;
  SemaphoreHandle_t mutex;

  uint32_t default_recv_timeout;
};

static void _internal_recv_callback(uint8_t connect_id);
static void _internal_close_callback(uint8_t connect_id);

static tcp_socket_t *g_sockets[GSM_TCP_MAX_SOCKETS] = {NULL};

tcp_socket_t *tcp_socket_create(gsm_t *gsm, uint8_t connect_id) {
  if (!gsm || connect_id >= GSM_TCP_MAX_SOCKETS) {
    return NULL;
  }

  tcp_socket_t *sock = (tcp_socket_t *)pvPortMalloc(sizeof(tcp_socket_t));
  if (!sock) {
    return NULL;
  }

  memset(sock, 0, sizeof(tcp_socket_t));
  sock->gsm = gsm;
  sock->connect_id = connect_id;
  sock->is_connected = false;
  sock->is_closed_by_peer = false;
  sock->default_recv_timeout = 5000;

  sock->rx_queue = xQueueCreate(10, sizeof(tcp_pbuf_t *));
  if (!sock->rx_queue) {
    vPortFree(sock);
    return NULL;
  }

  sock->mutex = xSemaphoreCreateMutex();
  if (!sock->mutex) {
    vQueueDelete(sock->rx_queue);
    vPortFree(sock);
    return NULL;
  }

  g_sockets[connect_id] = sock;

  return sock;
}

int tcp_connect(tcp_socket_t *sock, uint8_t context_id, const char *remote_ip,
                uint16_t remote_port, uint32_t timeout_ms) {
  if (!sock || !remote_ip) {
    return -1;
  }

  int ret = gsm_tcp_open(sock->gsm, sock->connect_id, context_id, remote_ip,
                         remote_port,
                         0,
                         _internal_recv_callback, _internal_close_callback,
                         NULL);

  if (ret == 0) {
    if (xSemaphoreTake(sock->mutex, portMAX_DELAY) == pdTRUE) {
      sock->is_connected = true;
      sock->is_closed_by_peer = false;
      xSemaphoreGive(sock->mutex);
    }
  }

  return ret;
}

int tcp_send(tcp_socket_t *sock, const uint8_t *data, size_t len) {
  if (!sock || !data || len == 0) {
    return -1;
  }

  if (!sock->is_connected) {
    return -1;
  }

  int ret = gsm_tcp_send(sock->gsm, sock->connect_id, data, len, NULL);

  if (ret == 0) {
    return (int)len;
  } else {
    return -1;
  }
}

void tcp_set_recv_timeout(tcp_socket_t *sock, uint32_t timeout_ms) {
  if (!sock) {
    return;
  }

  if (xSemaphoreTake(sock->mutex, portMAX_DELAY) == pdTRUE) {
    sock->default_recv_timeout = timeout_ms;
    xSemaphoreGive(sock->mutex);
  }
}

int tcp_recv(tcp_socket_t *sock, uint8_t *buf, size_t len,
             uint32_t timeout_ms) {
  if (!sock || !buf || len == 0) {
    return -1;
  }

  if (timeout_ms == 0) {
    if (xSemaphoreTake(sock->mutex, portMAX_DELAY) == pdTRUE) {
      timeout_ms = sock->default_recv_timeout;
      xSemaphoreGive(sock->mutex);
    } else {
      timeout_ms = 5000;
    }
  }

  TickType_t timeout_ticks =
      (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

  tcp_pbuf_t *pbuf = NULL;
  if (xQueueReceive(sock->rx_queue, &pbuf, timeout_ticks) == pdTRUE) {
    if (!pbuf) {
      return -1; 
    }

    size_t copy_len = (pbuf->len < len) ? pbuf->len : len;
    memcpy(buf, pbuf->payload, copy_len);

    tcp_pbuf_free(pbuf);

    return (int)copy_len;
  } else {
    LOG_ERR("tcp_recv: 타임아웃");
    return 0;
  }
}

int tcp_close(tcp_socket_t *sock) {
  if (!sock) {
    return -1;
  }

  if (!sock->is_connected) {
    return 0;
  }

  int ret = gsm_tcp_close(sock->gsm, sock->connect_id, NULL);

  if (xSemaphoreTake(sock->mutex, portMAX_DELAY) == pdTRUE) {
    sock->is_connected = false;
    xSemaphoreGive(sock->mutex);
  }

  return ret;
}

int tcp_close_force(tcp_socket_t *sock) {

  if (!sock) {
    return -1;
  }

  int ret = gsm_tcp_close_force(sock->gsm, sock->connect_id);

  if (xSemaphoreTake(sock->mutex, portMAX_DELAY) == pdTRUE) {
    sock->is_connected = false;
    sock->is_closed_by_peer = false;
    xSemaphoreGive(sock->mutex);
  }

  return ret;
}
void tcp_socket_destroy(tcp_socket_t *sock) {
  if (!sock) {
    return;
  }

  if (sock->is_connected) {
    tcp_close(sock);
  }

  tcp_pbuf_t *pbuf;
  while (xQueueReceive(sock->rx_queue, &pbuf, 0) == pdTRUE) {
    if (pbuf) {
      tcp_pbuf_free(pbuf);
    }
  }

  g_sockets[sock->connect_id] = NULL;

  vQueueDelete(sock->rx_queue);
  vSemaphoreDelete(sock->mutex);
  vPortFree(sock);
}

size_t tcp_available(tcp_socket_t *sock) {
  if (!sock) {
    return 0;
  }

  return uxQueueMessagesWaiting(sock->rx_queue);
}


static void _internal_recv_callback(uint8_t connect_id) {
  if (connect_id >= GSM_TCP_MAX_SOCKETS) {
    return;
  }

  tcp_socket_t *sock = g_sockets[connect_id];
  if (!sock || !sock->gsm) {
    return;
  }

  tcp_pbuf_t *pbuf = NULL;
  if (xSemaphoreTake(sock->gsm->tcp.tcp_mutex, portMAX_DELAY) == pdTRUE) {
    pbuf = tcp_pbuf_dequeue(&sock->gsm->tcp.sockets[connect_id]);
    xSemaphoreGive(sock->gsm->tcp.tcp_mutex);
  }

  if (!pbuf) {
    return;
  }

  if (xQueueSend(sock->rx_queue, &pbuf, 0) != pdTRUE) {
    tcp_pbuf_free(pbuf);
  }
}

static void _internal_close_callback(uint8_t connect_id) {
  if (connect_id >= GSM_TCP_MAX_SOCKETS) {
    return;
  }

  tcp_socket_t *sock = g_sockets[connect_id];
  if (!sock) {
    return;
  }

  if (xSemaphoreTake(sock->mutex, portMAX_DELAY) == pdTRUE) {
    sock->is_connected = false;
    sock->is_closed_by_peer = true;
    xSemaphoreGive(sock->mutex);
  }

  tcp_pbuf_t *null_pbuf = NULL;
  xQueueSend(sock->rx_queue, &null_pbuf, 0);
}

gsm_tcp_state_t tcp_get_socket_state(tcp_socket_t *sock, uint8_t id) {
  return sock->gsm->tcp.sockets[id].state;
}
