#include <stdio.h>
#include <stdbool.h>
#include "gps_ubx.h"
#include "ubx_init.h"
#include <stdlib.h>
#include <string.h>

#ifndef TAG
    #define TAG "UBX_INIT"
#endif

#include "log.h"

#define CFG_GGA_UART1 (0x209100bbU)
#define CFG_GLL_UART1 (0x209100caU)
#define CFG_GSA_UART1 (0x209100c0U)
#define CFG_GSV_UART1 (0x209100c5U)
#define CFG_RMC_UART1 (0x209100acU)
#define CFG_VTG_UART1 (0x209100b1U)

#define CFG_NAV_HPPOSLLH_UART1 (0x20910034U)
#define CFG_NAV_RELPOSNED_UART1 (0x2091008eU)

#define CFG_RTCM_1005_UART1 (0x209102beU) // antenna
#define CFG_RTCM_1005_UART2 (0x209102bfU) // antenna
#define CFG_RTCM_1074_UART1 (0x2091035fU) // GPS
#define CFG_RTCM_1074_UART2 (0x20910360U) // GPS
#define CFG_RTCM_1084_UART1 (0x20910364U) // GLONASS
#define CFG_RTCM_1084_UART2 (0x20910365U) // GLONASS
#define CFG_RTCM_1094_UART1 (0x20910369U) // GALLILEO
#define CFG_RTCM_1094_UART2 (0x2091036aU) // GALLILEO
#define CFG_RTCM_1124_UART1 (0x2091036eU) // BEIDU
#define CFG_RTCM_1124_UART2 (0x2091036fU) // BEIDU
#define CFG_RTCM_4072_0_UART2 (0x20910300U) // RTCM4072_0
#define CFG_RTCM_4072_1_UART2 (0x20910383U) // RTCM4072_1

#define CFG_BAUDRATE_UART1 (0x40520001U) // 4바이트
#define CFG_BAUDRATE_UART2 (0x40530001U) // 4바이트

#define CFG_UART1INPROT_RTCM3X (0x10730004U)
#define CFG_UART1OUTPROT_RTCM3X (0x10740004U)
#define CFG_UART2INPROT_RTCM3X (0x10750004U)
#define CFG_UART2OUTPROT_RTCM3X (0x10760004U)

#define CFG_RATE_MEAS (0x30210001U)

/* base station */
#define CFG_TMODE_MODE (0x20030001U) // 0 disable 1 survey 2 fixed
#define CFG_TMODE_POS_TYPE (0x20030002U) // 0 ecef 1 llh
#define CFG_TMODE_LLH_LAT (0x40030009U) // I4
#define CFG_TMODE_LLH_LON (0x4003000aU) // I4
#define CFG_TMODE_LLH_HEIGHT (0x4003000bU) // I4
#define CFG_TMODE_SURVEY_DUR (0x40030010U)  // U4 seconds
#define CFG_TMODE_SURVEY_ACC_LIMIT (0x40030011U)  // U2 pos accuracy 0.1mm 단위

static const ubx_cfg_item_t ublox_base_configs[] = {

    /* NMEA OUTPUT 설정 */
    {
        .key_id = CFG_GLL_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_GSA_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_GSV_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_RMC_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_VTG_UART1,
        .value = {0},
        .value_len = 1,
    },

    /* UBX 메시지 설정 */
    {
        .key_id = CFG_NAV_HPPOSLLH_UART1,
        .value = {1},
        .value_len = 1,
    },

    /* RTCM 설정 */
    {
        .key_id = CFG_RTCM_1005_UART1,
        .value = {10},
        .value_len = 1,
    },

    {
        .key_id = CFG_RTCM_1074_UART1,
        .value = {1},
        .value_len = 1,
    },

    // {
    //     .key_id = CFG_RTCM_1084_UART1,
    //     .value = {2},
    //     .value_len = 1,
    // },

    {
        .key_id = CFG_RTCM_1094_UART1,
        .value = {1},
        .value_len = 1,
    },

    {
        .key_id = CFG_RTCM_1124_UART1,
        .value = {1},
        .value_len = 1,
    },

    {
        .key_id = CFG_TMODE_POS_TYPE,
        .value = {1},
        .value_len = 1,
    },

    {
        .key_id = CFG_UART1INPROT_RTCM3X,
        .value = {1},
        .value_len = 1,
    },

    {
        .key_id = CFG_UART1OUTPROT_RTCM3X,
        .value = {1},
        .value_len = 1,
    },

    // {
    //     .key_id = CFG_TMODE_SURVEY_DUR,
    //     .value = {0x78, 0x00, 0x00, 0x00}, // 120 sec
    //     .value_len = 4,
    // },

    // {
    //     .key_id = CFG_TMODE_SURVEY_ACC_LIMIT,
    //     .value = {0x88, 0x13, 0x00, 0x00}, // 5m
    //     .value_len = 4,
    // }
};

static const ubx_cfg_item_t ublox_rover_configs[] = {
    {
        .key_id = CFG_BAUDRATE_UART2,
        .value = {0x00, 0xC2, 0x01, 0x00},
        .value_len = 4,
    },

    {
        .key_id = CFG_RATE_MEAS,
        .value = {0x32, 0x00}, // 50ms
        .value_len = 2,
    },

    /* NMEA OUTPUT 설정 */
    {
        .key_id = CFG_GGA_UART1,
        .value = {20},
        .value_len = 1,
    },

    {
        .key_id = CFG_GLL_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_GSA_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_GSV_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_RMC_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_VTG_UART1,
        .value = {0},
        .value_len = 1,
    },

    /* UBX 메시지 설정 */

    {
        .key_id = CFG_NAV_RELPOSNED_UART1,
        .value = {1},
        .value_len = 1,
    },

    /* UART2 포트 설정 */
    {
        .key_id = CFG_UART2INPROT_RTCM3X,
        .value = {1},
        .value_len = 1,
    },

    /* F9H는 기본적으로 비활성화 되어있어서 NACK 수신받음 */
    // {
    //     .key_id = CFG_UART2OUTPROT_RTCM3X,
    //     .value = {0},
    //     .value_len = 1,
    // },
};

static const ubx_cfg_item_t ublox_moving_base_configs[] = {
    {
        .key_id = CFG_BAUDRATE_UART2,
        .value = {0x00, 0xC2, 0x01, 0x00},
        .value_len = 4,
    },

    {
        .key_id = CFG_RATE_MEAS,
        .value = {0x32, 0x00}, // 50ms
        .value_len = 2,
    },
    
    /* nmea 설정 */
    {
        .key_id = CFG_GGA_UART1,
        .value = {20},
        .value_len = 1,
    },

    {
        .key_id = CFG_GLL_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_GSA_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_GSV_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_RMC_UART1,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_VTG_UART1,
        .value = {0},
        .value_len = 1,
    },

    /* ubx 프로토콜 설정 */
    {
        .key_id = CFG_NAV_HPPOSLLH_UART1,
        .value = {1},
        .value_len = 1,
    },
    
    /* RTCM 설정 */
    {
        .key_id = CFG_RTCM_1005_UART2,
        .value = {200},
        .value_len = 1,
    },

    {
        .key_id = CFG_RTCM_1074_UART2,
        .value = {20},
        .value_len = 1,
    },

    // {
    //     .key_id = CFG_RTCM_1084_UART2,
    //     .value = {1},
    //     .value_len = 1,
    // },

    {
        .key_id = CFG_RTCM_1094_UART2,
        .value = {20},
        .value_len = 1,
    },

    {
        .key_id = CFG_RTCM_1124_UART2,
        .value = {20},
        .value_len = 1,
    },

    {
        .key_id = CFG_RTCM_4072_0_UART2,
        .value = {20},
        .value_len = 1,
    },

    {
        .key_id = CFG_RTCM_4072_1_UART2,
        .value = {20},
        .value_len = 1,
    },

    {
        .key_id = CFG_UART1INPROT_RTCM3X,
        .value = {20},
        .value_len = 1,
    },

    /* UART2 포트 설정 */
    {
        .key_id = CFG_UART2INPROT_RTCM3X,
        .value = {0},
        .value_len = 1,
    },

    {
        .key_id = CFG_UART2OUTPROT_RTCM3X,
        .value = {1},
        .value_len = 1,
    },
};

static void on_init_complete(bool success, size_t failed_step, void *user_data)
{
    if (success)
    {
        LOG_DEBUG("✓ UBX initialization completed successfully!\n");
    }
    else
    {
        LOG_ERR("✗ UBX initialization failed at step %zu\n", failed_step);
    }
}

bool ubx_rover_init(gps_t* gps)
{
    ubx_init_async_start(gps, UBX_CFG_LAYER_RAM,
                          ublox_rover_configs, sizeof(ublox_rover_configs) / sizeof(ublox_rover_configs[0]),
        on_init_complete, NULL);
}

bool ubx_base_init(gps_t* gps)
{
    ubx_init_async_start(gps, UBX_CFG_LAYER_RAM,
                          ublox_base_configs, sizeof(ublox_base_configs) / sizeof(ublox_base_configs[0]),
        on_init_complete, NULL);
}

bool ubx_moving_base_init(gps_t* gps)
{
    ubx_init_async_start(gps, UBX_CFG_LAYER_RAM,
                          ublox_moving_base_configs, sizeof(ublox_moving_base_configs) / sizeof(ublox_moving_base_configs[0]),
        on_init_complete, NULL);
}

static void on_factory_reset_complete(bool ack, void *user_data)
{
    gps_t *gps = (gps_t *)user_data;
    ubx_init_context_t *ctx = &gps->ubx_init_ctx;

    if (ack)
    {
        LOG_DEBUG("✓ F9P factory reset completed successfully!\n");
        ctx->state = UBX_INIT_STATE_DONE;

        if (ctx->on_complete)
        {
            ctx->on_complete(true, 0, ctx->user_data);
        }
    }
    else
    {
        LOG_ERR("✗ F9P factory reset failed\n");
        ctx->state = UBX_INIT_STATE_ERROR;

        if (ctx->on_complete)
        {
            ctx->on_complete(false, 0, ctx->user_data);
        }
    }
}

bool ubx_factory_reset(gps_t* gps, ubx_init_complete_callback_t callback, void *user_data)
{
    ubx_init_context_t *ctx = &gps->ubx_init_ctx;

    if (ctx->state == UBX_INIT_STATE_RUNNING)
    {
        return false;
    }

    ctx->state = UBX_INIT_STATE_RUNNING;
    ctx->on_complete = callback;
    ctx->user_data = user_data;

    // UBX-CFG-CFG 메시지 전송
    // clearMask: 0x1F (모든 섹션 클리어)
    // saveMask: 0x00 (저장 안 함)
    // loadMask: 0x1F (모든 섹션 로드 = 팩토리 리셋)
    if (!ubx_send_cfg_cfg(gps, 0x0000001F, 0x00000000, 0x0000001F,
                          on_factory_reset_complete, gps))
    {
        ctx->state = UBX_INIT_STATE_ERROR;
        return false;
    }

    LOG_DEBUG("F9P factory reset initiated...\n");

    return true;
}

/**
 * @brief Time Mode 비활성화
 *
 * @param gps GPS 구조체
 * @return true 성공, false 실패
 */

bool ubx_disable_time_mode(gps_t* gps)
{
    if (!gps) {
        return false;
    }

    ubx_cfg_item_t tmode_config = {
        .key_id = CFG_TMODE_MODE,
        .value = {0},  // 0 = Disabled
        .value_len = 1,
    };

    bool result = ubx_send_valset_sync(gps, UBX_CFG_LAYER_RAM, &tmode_config, 1, 3000);

    if (result) {
        LOG_DEBUG("Time mode disabled\n");
    } else {
        LOG_ERR("Failed to disable time mode\n");
    }

    return result;
}

typedef struct {
    gps_t* gps;
    ubx_cfg_item_t position_configs[3];  // LAT, LON, HEIGHT
    ubx_cfg_item_t mode_config;          // MODE
    ubx_init_complete_callback_t user_callback;
    void* user_data;
    char lat_str[16];
    char lon_str[16];
    char alt_str[16];
} tmode_async_context_t;

static tmode_async_context_t g_tmode_ctx;

static void on_mode_enable_complete(bool ack, void *user_data)
{
    tmode_async_context_t *ctx = (tmode_async_context_t *)user_data;

    if (ack) {
        LOG_DEBUG("Fixed mode enabled\n");
        if (ctx->user_callback) {
            ctx->user_callback(true, 0, ctx->user_data);
        }
    } else {
        LOG_ERR("Failed to enable fixed mode\n");
        if (ctx->user_callback) {
            ctx->user_callback(false, 1, ctx->user_data);
        }
    }
}


static void on_position_set_complete(bool ack, void *user_data)
{
    tmode_async_context_t *ctx = (tmode_async_context_t *)user_data;

    if (!ack) {
        LOG_ERR("Failed to set position coordinates\n");
        if (ctx->user_callback) {
            ctx->user_callback(false, 0, ctx->user_data);
        }

        return;
    }

    LOG_DEBUG("Position set (lat: %s, lon: %s, alt: %s m)\n",
              ctx->lat_str, ctx->lon_str, ctx->alt_str);

    bool result = ubx_send_valset_cb(ctx->gps, UBX_CFG_LAYER_RAM,
                                     &ctx->mode_config, 1,
                                     on_mode_enable_complete, ctx);

    if (!result) {
        LOG_ERR("Failed to send mode enable command\n");
        if (ctx->user_callback) {
            ctx->user_callback(false, 1, ctx->user_data);
        }
    }
}


/**
 * @brief Fixed 모드 설정 (수동 좌표 입력) - 비동기 버전
 *
 * @param gps GPS 구조체
 * @param lat_str 위도 문자열 (degrees, 예: "37.12345")
 * @param lon_str 경도 문자열 (degrees, 예: "127.12345")
 * @param alt_str 고도 문자열 (meters, 예: "100.5")
 * @param callback 완료 시 호출될 콜백
 * @param user_data 콜백에 전달할 사용자 데이터
 * @return true 시작 성공, false 실패
 */

bool ubx_set_fixed_position_async(gps_t* gps, const char* lat_str, const char* lon_str,
                                   const char* alt_str, ubx_init_complete_callback_t callback,
                                   void *user_data)
{

    if (!gps || !lat_str || !lon_str || !alt_str) {
        return false;
    }
    // Context 초기화
    g_tmode_ctx.gps = gps;
    g_tmode_ctx.user_callback = callback;
    g_tmode_ctx.user_data = user_data;

    // 문자열 저장 (로그용)
    snprintf(g_tmode_ctx.lat_str, sizeof(g_tmode_ctx.lat_str), "%s", lat_str);
    snprintf(g_tmode_ctx.lon_str, sizeof(g_tmode_ctx.lon_str), "%s", lon_str);
    snprintf(g_tmode_ctx.alt_str, sizeof(g_tmode_ctx.alt_str), "%s", alt_str);

    // 문자열을 double로 변환
    double lat_deg = strtod(lat_str, NULL);
    double lon_deg = strtod(lon_str, NULL);
    double alt_m = strtod(alt_str, NULL);

    // u-blox 포맷으로 변환
    int32_t lat_e7 = (int32_t)(lat_deg * 1e7);
    int32_t lon_e7 = (int32_t)(lon_deg * 1e7);
    int32_t height_cm = (int32_t)(alt_m * 100);

    // STEP 1: 위치 정보 설정
    g_tmode_ctx.position_configs[0].key_id = CFG_TMODE_LLH_LAT;
    g_tmode_ctx.position_configs[0].value[0] = (lat_e7 & 0xFF);
    g_tmode_ctx.position_configs[0].value[1] = (lat_e7 >> 8) & 0xFF;
    g_tmode_ctx.position_configs[0].value[2] = (lat_e7 >> 16) & 0xFF;
    g_tmode_ctx.position_configs[0].value[3] = (lat_e7 >> 24) & 0xFF;
    g_tmode_ctx.position_configs[0].value_len = 4;

    g_tmode_ctx.position_configs[1].key_id = CFG_TMODE_LLH_LON;
    g_tmode_ctx.position_configs[1].value[0] = (lon_e7 & 0xFF);
    g_tmode_ctx.position_configs[1].value[1] = (lon_e7 >> 8) & 0xFF;
    g_tmode_ctx.position_configs[1].value[2] = (lon_e7 >> 16) & 0xFF;
    g_tmode_ctx.position_configs[1].value[3] = (lon_e7 >> 24) & 0xFF;
    g_tmode_ctx.position_configs[1].value_len = 4;

    g_tmode_ctx.position_configs[2].key_id = CFG_TMODE_LLH_HEIGHT;
    g_tmode_ctx.position_configs[2].value[0] = (height_cm & 0xFF);
    g_tmode_ctx.position_configs[2].value[1] = (height_cm >> 8) & 0xFF;
    g_tmode_ctx.position_configs[2].value[2] = (height_cm >> 16) & 0xFF;
    g_tmode_ctx.position_configs[2].value[3] = (height_cm >> 24) & 0xFF;
    g_tmode_ctx.position_configs[2].value_len = 4;

    // STEP 2: Mode 설정 준비
    g_tmode_ctx.mode_config.key_id = CFG_TMODE_MODE;
    g_tmode_ctx.mode_config.value[0] = 2;  // Fixed mode
    g_tmode_ctx.mode_config.value_len = 1;

    // STEP 1 실행: 위치 정보 전송
    bool result = ubx_send_valset_cb(gps, UBX_CFG_LAYER_RAM,
                                     g_tmode_ctx.position_configs, 3,
                                     on_position_set_complete, &g_tmode_ctx);

    if (!result) {
        LOG_ERR("Failed to send position config command\n");
        return false;
    }

    LOG_DEBUG("Fixed position async started\n");

    return true;
}

typedef struct {
    gps_t* gps;
    ubx_cfg_item_t survey_configs[3];  // MODE, MIN_DUR, ACC_LIMIT
    ubx_init_complete_callback_t user_callback;
    void* user_data;
    uint32_t min_duration;
    uint32_t accuracy_limit;
} survey_async_context_t;

static survey_async_context_t g_survey_ctx;

static void on_survey_in_complete(bool ack, void *user_data)
{
    survey_async_context_t *ctx = (survey_async_context_t *)user_data;

    if (ack) {
        LOG_DEBUG("Survey-in mode started (duration: %u s, accuracy: %u mm)\n",
                  ctx->min_duration, ctx->accuracy_limit / 10);
        if (ctx->user_callback) {
            ctx->user_callback(true, 0, ctx->user_data);
        }
    } else {
        LOG_ERR("Failed to start survey-in mode\n");
        if (ctx->user_callback) {
            ctx->user_callback(false, 0, ctx->user_data);
        }
    }
}

bool ubx_set_survey_in_mode_async(gps_t* gps, uint32_t min_duration, uint32_t accuracy_limit,
                                   ubx_init_complete_callback_t callback, void *user_data)
{
    if (!gps) {
        return false;
    }

    g_survey_ctx.gps = gps;
    g_survey_ctx.user_callback = callback;
    g_survey_ctx.user_data = user_data;
    g_survey_ctx.min_duration = min_duration;
    g_survey_ctx.accuracy_limit = accuracy_limit;

    // Survey-in 설정 (Mode, Duration, Accuracy Limit)
    g_survey_ctx.survey_configs[0].key_id = CFG_TMODE_SURVEY_DUR;
    g_survey_ctx.survey_configs[0].value[0] = (min_duration & 0xFF);
    g_survey_ctx.survey_configs[0].value[1] = (min_duration >> 8) & 0xFF;
    g_survey_ctx.survey_configs[0].value[2] = (min_duration >> 16) & 0xFF;
    g_survey_ctx.survey_configs[0].value[3] = (min_duration >> 24) & 0xFF;
    g_survey_ctx.survey_configs[0].value_len = 4;

    g_survey_ctx.survey_configs[1].key_id = CFG_TMODE_SURVEY_ACC_LIMIT;
    g_survey_ctx.survey_configs[1].value[0] = (accuracy_limit & 0xFF);
    g_survey_ctx.survey_configs[1].value[1] = (accuracy_limit >> 8) & 0xFF;
    g_survey_ctx.survey_configs[1].value[2] = (accuracy_limit >> 16) & 0xFF;
    g_survey_ctx.survey_configs[1].value[3] = (accuracy_limit >> 24) & 0xFF;
    g_survey_ctx.survey_configs[1].value_len = 4;

    g_survey_ctx.survey_configs[2].key_id = CFG_TMODE_MODE;
    g_survey_ctx.survey_configs[2].value[0] = 1;  // Survey-in mode
    g_survey_ctx.survey_configs[2].value_len = 1;

    // 비동기 전송
    bool result = ubx_send_valset_cb(gps, UBX_CFG_LAYER_RAM,
                                     g_survey_ctx.survey_configs, 3,
                                     on_survey_in_complete, &g_survey_ctx);

    if (!result) {
        LOG_ERR("Failed to send survey-in command\n");
        return false;
    }

    LOG_DEBUG("Survey-in mode async started\n");

    return true;
}
