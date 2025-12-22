#include "rtcm.h"
#include "lora_app.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

#ifndef TAG
  #define TAG "RTCM"
#endif

#include "log.h"

// HEX ASCII로 변환하면 데이터가 2배 증가:
// LoRa 최대 236 HEX 문자 = 118 바이트 binary
#define RTCM_MAX_FRAGMENT_SIZE 118  // Max binary size per fragment

// LoRa Time on Air calculation (SF7, BW125, CR4/5, Preamble 8)
// HEX 변환: 1 byte -> 2 HEX chars
// 최대 118바이트 = 236 HEX chars = 350ms (측정값)
// 20% 여유: 350ms * 1.2 = 420ms
#define RTCM_MAX_BINARY_BYTES 118   // 최대 binary 크기
#define LORA_TOA_MAX_MS 350         // 118바이트(236 HEX) 전송 시간
#define LORA_TOA_MARGIN_PERCENT 20  // 20% margin

/**
 * @brief Calculate LoRa Time on Air (ToA) with 20% margin
 *
 * Formula: ToA = (bytes / 118) * 350ms * 1.2
 *
 * @param binary_bytes Binary payload size (before HEX conversion)
 * @return Time on Air in milliseconds (with 20% margin)
 */
static uint32_t calculate_lora_toa(size_t binary_bytes) {
  // ToA(ms) = (bytes / 118) * 350 * 1.2
  uint32_t toa_ms = (binary_bytes * LORA_TOA_MAX_MS / RTCM_MAX_BINARY_BYTES);
  toa_ms = toa_ms * (100 + LORA_TOA_MARGIN_PERCENT) / 100;

  // Minimum ToA
  if (toa_ms < 60) {
    toa_ms = 60;  // 최소 60ms (20% margin 포함)
  }

  return toa_ms;
}

/**
 * @brief Callback for last fragment transmission completion
 */
static void rtcm_last_fragment_callback(bool success, void *user_data) {
  uint16_t msg_type = (uint16_t)(uintptr_t)user_data;

  if (success) {
    LOG_INFO("RTCM transmission complete (type=%d)", msg_type);
  } else {
    LOG_ERR("RTCM last fragment transmission failed (type=%d)", msg_type);
  }
}

void rtcm_tx_task_init(void) {
  // No task needed anymore - direct async transmission
  LOG_INFO("RTCM async transmission initialized (no task)");
}

bool rtcm_send_to_lora(gps_t *gps) {
  if (!gps) {
    LOG_ERR("GPS handle is NULL");
    return false;
  }

  // RTCM packet total length
  size_t rtcm_len = gps->rtcm.total_len;

  if (rtcm_len == 0) {
    LOG_ERR("RTCM length is zero");
    return false;
  }

  // Calculate total fragments needed
  uint8_t total_fragments = (rtcm_len + RTCM_MAX_FRAGMENT_SIZE - 1) / RTCM_MAX_FRAGMENT_SIZE;

  LOG_INFO("RTCM TX: type=%d, len=%d, fragments=%d",
           gps->rtcm.msg_type, rtcm_len, total_fragments);

  // Queue all fragments at once
  for (uint8_t i = 0; i < total_fragments; i++) {
    size_t offset = i * RTCM_MAX_FRAGMENT_SIZE;
    size_t fragment_len = (rtcm_len - offset > RTCM_MAX_FRAGMENT_SIZE)
                          ? RTCM_MAX_FRAGMENT_SIZE
                          : (rtcm_len - offset);

    uint32_t toa_ms = calculate_lora_toa(fragment_len);

    LOG_DEBUG("Queueing fragment %d/%d: %d bytes",
             i + 1, total_fragments, fragment_len);

    // Last fragment gets callback for logging
    bool is_last = (i == total_fragments - 1);
    lora_command_callback_t callback = is_last ? rtcm_last_fragment_callback : NULL;
    void *user_data = is_last ? (void*)(uintptr_t)gps->rtcm.msg_type : NULL;

    if (!lora_send_p2p_raw_async(&gps->payload[offset], fragment_len, toa_ms,
                                  callback, user_data)) {
      LOG_ERR("Failed to queue fragment %d/%d - LoRa TX queue full?", i + 1, total_fragments);
      return false;
    }
  }

  // All fragments queued successfully - return immediately (non-blocking)
  LOG_INFO("All %d fragments queued to LoRa TX task", total_fragments);
  return true;
}
