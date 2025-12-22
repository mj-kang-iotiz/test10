#include "ble_cmd.h"
#include "board_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "flash_params.h"
#include "ble.h"
#include "ble_app.h"

#ifndef TAG
#define TAG "BLE_CMD"
#endif

#include "log.h"

static const char *ble_resp_str_ok = "OK\n";
static const char *ble_resp_str_invalid = "+E01\n";
static const char *ble_resp_str_param_err = "+E02\n";
static const char *ble_resp_str_not_rdy = "+E03\n";
static const char *ble_resp_str_err = "+ERROR\n";

#define BLE_AT_RESP_SEND(data) ble_send(data, strlen(data), false)

#define BLE_AT_RESP_SEND_OK() BLE_AT_RESP_SEND(ble_resp_str_ok)
#define BLE_AT_RESP_SEND_INVALID() BLE_AT_RESP_SEND(ble_resp_str_invalid)
#define BLE_AT_RESP_SEND_PARAM_ERR() BLE_AT_RESP_SEND(ble_resp_str_param_err)
#define BLE_AT_RESP_SEND_NOT_RDY() BLE_AT_RESP_SEND(ble_resp_str_not_rdy)
#define BLE_AT_RESP_SEND_ERR() BLE_AT_RESP_SEND(ble_resp_str_err)

static void sd_handler(ble_instance_t *inst, const char *param);
static void sc_handler(ble_instance_t *inst, const char *param);
static void sm_handler(ble_instance_t *inst, const char *param);
static void si_handler(ble_instance_t *inst, const char *param);
static void sp_handler(ble_instance_t *inst, const char *param);
static void sg_handler(ble_instance_t *inst, const char *param);
static void ss_handler(ble_instance_t *inst, const char *param);
static void gd_handler(ble_instance_t *inst, const char *param);
static void gi_handler(ble_instance_t *inst, const char *param);
static void gp_handler(ble_instance_t *inst, const char *param);
static void gg_handler(ble_instance_t *inst, const char *param);
static void rs_handler(ble_instance_t *inst, const char *param);

void bot_ok_handler(ble_instance_t *inst, const char *param)
{
    LOG_DEBUG("BLE AT OK received");
}

void bot_err_handler(ble_instance_t *inst, const char *param)
{
    LOG_DEBUG("BLE AT ERROR received");
}

void bot_rdy_handler(ble_instance_t *inst, const char *param)
{
    LOG_DEBUG("BLE AT READY received");
}

void bot_advertising_handler(ble_instance_t *inst, const char *param)
{
    LOG_DEBUG("BLE AT ADVERTISING");
}

void bot_connected_handler(ble_instance_t *inst, const char *param)
{
    LOG_DEBUG("BLE AT CONNECTED");
}

void bot_disconnected_handler(ble_instance_t *inst, const char *param)
{
    LOG_DEBUG("BLE AT DISCONNECTED");
}

// AT+UART=xxxx
// AT+MANUF=xxxxxxxx
static const ble_at_cmd_entry_t bot_cmd_table[] = {
    {"+OK", bot_ok_handler},
    {"+ERROR", bot_err_handler},
    {"+READY", bot_rdy_handler},
    {"+ADVERTISING", bot_advertising_handler},
    {"+CONNECTED", bot_connected_handler},
    {"+DISCONNECTED", bot_disconnected_handler},
};

static const ble_at_cmd_entry_t at_cmd_table[] = {
    {"SD+", sd_handler},
    {"SC+", sc_handler},
    {"SM+", sm_handler},
    {"SI+", si_handler},
    {"SP+", sp_handler},
    {"SG+", sg_handler},
    {"SS", ss_handler},
    {"GD", gd_handler},
    {"GI", gi_handler},
    {"GP", gp_handler},
    {"GG", gg_handler},
    {"RS", rs_handler},
    {NULL, NULL}};

void ble_app_cmd_handler(ble_instance_t *inst)
{
    for (int i = 0; at_cmd_table[i].name != NULL; i++)
    {
        size_t name_len = strlen(at_cmd_table[i].name);

        if (strncmp(inst->parser.data, at_cmd_table[i].name, name_len) == 0)
        {
            at_cmd_table[i].handler(inst, inst->parser.data + name_len);
            return;
        }
    }
}

void ble_at_cmd_handler(ble_instance_t *inst)
{
    if (inst->async_request != NULL && inst->async_request->status == BLE_AT_STATUS_PENDING)
    {

        // 기대하는 응답과 매칭되는지 확인

        size_t expected_len = strlen(inst->async_request->expected_response);

        if (strncmp(inst->parser.data, inst->async_request->expected_response, expected_len) == 0)
        {

            // 응답 저장

            size_t data_len = strlen(inst->parser.data);

            if (data_len < BLE_AT_RESPONSE_MAX_SIZE)
            {

                memcpy(inst->async_request->response_buf, inst->parser.data, data_len);

                inst->async_request->response_len = data_len;

                inst->async_request->response_buf[data_len] = '\0';
            }
            else
            {

                memcpy(inst->async_request->response_buf, inst->parser.data, BLE_AT_RESPONSE_MAX_SIZE - 1);

                inst->async_request->response_len = BLE_AT_RESPONSE_MAX_SIZE - 1;

                inst->async_request->response_buf[BLE_AT_RESPONSE_MAX_SIZE - 1] = '\0';
            }

            // 상태 업데이트

            if (strncmp(inst->parser.data, "+OK", 3) == 0)
            {

                inst->async_request->status = BLE_AT_STATUS_COMPLETED;
            }
            else if (strncmp(inst->parser.data, "+ERROR", 6) == 0)
            {

                inst->async_request->status = BLE_AT_STATUS_ERROR;
            }
            else
            {

                inst->async_request->status = BLE_AT_STATUS_COMPLETED;
            }

            // 세마포어 해제 (대기 중인 태스크 깨우기)

            if (inst->async_request->wait_sem != NULL)
            {

                xSemaphoreGive(inst->async_request->wait_sem);
            }

            LOG_INFO("Async AT response matched: %s", inst->parser.data);

            return;
        }
    }

    // 비동기 AT 명령어 요청이 있는지 확인 (콜백 기반)

    if (inst->async_at_cmd.is_active)
    {

        // +OK 또는 +ERROR 응답 확인

        bool is_ok = (strncmp(inst->parser.data, "+OK", 3) == 0);

        bool is_error = (strncmp(inst->parser.data, "+ERROR", 6) == 0);

        if (is_ok || is_error)
        {

            LOG_INFO("Async AT command response: %s", inst->parser.data);

            // 콜백 호출

            if (inst->async_at_cmd.callback)
            {

                inst->async_at_cmd.callback(is_ok, inst->async_at_cmd.user_data);
            }

            // Bypass 모드로 복귀

            if (inst->ble.ops && inst->ble.ops->bypass_mode)
            {

                inst->ble.ops->bypass_mode();
            }

            inst->current_mode = BLE_MODE_BYPASS;

            // 비활성화

            inst->async_at_cmd.is_active = false;

            LOG_INFO("Switched back to bypass mode");

            return;
        }
    }

    // 비동기 요청이 없거나 매칭되지 않으면 기존 핸들러 실행
    for (int i = 0; bot_cmd_table[i].name != NULL; i++)
    {
        size_t name_len = strlen(bot_cmd_table[i].name);

        if (strncmp(inst->parser.data, bot_cmd_table[i].name, name_len) == 0)
        {
            bot_cmd_table[i].handler(inst, inst->parser.data + name_len);
            return;
        }
    }
}

static void sd_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    char buf[100];
    char device_name[32];

    sscanf(param, "%[^\n]", device_name);

    ble_get_handle()->ops->at_mode();
    vTaskDelay(pdMS_TO_TICKS(20));
    ble_get_handle()->ops->send("AT\r", strlen("AT\r"));
    vTaskDelay(pdMS_TO_TICKS(100));
    ble_get_handle()->ops->send("AT+DISCONNECT\r", strlen("AT+DISCONNECT\r"));
    vTaskDelay(pdMS_TO_TICKS(500));
    sprintf(buf, "AT+MANUF=%s\r", device_name);
    ble_get_handle()->ops->send(buf, strlen(buf));

    char buffer[10];
    ble_uart5_recv_line_poll(buffer, sizeof(buffer), 20);
    if(strncmp(buffer, "+OK", 3) != 0)
    {
        ble_get_handle()->ops->bypass_mode();
        BLE_AT_RESP_SEND_ERR();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    
    ble_get_handle()->ops->bypass_mode();
    
    flash_params_set_ble_device_name(device_name);
    flash_params_erase();
    flash_params_write(params);

    sprintf(buf, "Set %s Complete\n\r", device_name);
    ble_send((uint8_t *)buf, strlen(buf), false);
}

static void sc_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    char buf[100];
    char caster_host[64];
    char caster_port[8];
    char *port_sep = strrchr(param, ':'); // 마지막 ':' 찾기
    if (port_sep)
    {
        *port_sep = '\0'; // host와 port 분리
        strncpy(caster_host, param, sizeof(caster_host) - 1);

        char *end = strchr(port_sep + 1, '\n');
        if (end)
            *end = '\0';
        strncpy(caster_port, port_sep + 1, sizeof(caster_port) - 1);
    }

    flash_params_set_ntrip_url((const char *)caster_host);
    flash_params_set_ntrip_port((const char *)caster_port);

    sprintf(buf, "Set %s:%s Complete\n\r", params->ntrip_url, params->ntrip_port);
    ble_send((uint8_t *)buf, strlen(buf), false);
}
static void sm_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    char buf[100];
    char mountpoint[32];
    char *end = strchr(param, '\n');
    if (end)
        *end = '\0';
    strncpy(mountpoint, param, sizeof(mountpoint) - 1);
    flash_params_set_ntrip_mountpoint(mountpoint);

    sprintf(buf, "Set %s Complete\n\r", params->ntrip_mountpoint);
    ble_send((uint8_t *)buf, strlen(buf), false);
}
static void si_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    char buf[100];
    char ntrip_id[32];
    char *end = strchr(param, '\n');
    if (end)
        *end = '\0';
    strncpy(ntrip_id, param, sizeof(ntrip_id) - 1);

    flash_params_set_ntrip_id(ntrip_id);

    sprintf(buf, "Set %s Complete\n\r", params->ntrip_id);
    ble_send((uint8_t *)buf, strlen(buf), false);
}
static void sp_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    char buf[100];
    char password[32];
    char *end = strchr(param, '\n');
    if (end)
        *end = '\0';
    strncpy(password, param, sizeof(password) - 1);
    flash_params_set_ntrip_pw(password);

    sprintf(buf, "Set %s Complete\n\r", params->ntrip_pw);
    ble_send((uint8_t *)buf, strlen(buf), false);
}
static void sg_handler(ble_instance_t *inst, const char *param)
{
    char buf[100];
    int ret = 0;
    user_params_t *params = flash_params_get_current();
    uint32_t flag;
    char lat_str[20], lon_str[20], alt_str[16];
    char ns;
    char ew;

    ret = sscanf(param, "%d,%[^,],%c,%[^,],%c,%[^\n]",
           &flag, lat_str, &ns, lon_str, &ew, alt_str);

    char *end;
    double lat, lon, alt;
    lat = strtod(lat_str, &end);
    if(end == lat_str)
    {
        BLE_AT_RESP_SEND_ERR();
        return;
    }

    lon = strtod(lon_str, &end);
    if(end == lon_str)
    {
        BLE_AT_RESP_SEND_ERR();
        return;
    }

    alt = strtod(alt_str, &end);
    if(end == alt_str)
    {
        BLE_AT_RESP_SEND_ERR();
        return;
    }

    if (ret == 6)
    {

        flash_params_set_manual_position(flag, lat_str, lon_str, alt_str);

        sprintf(buf, "Set %d,%lf,%lf,%lf\n\r", params->use_manual_position,
        		atof(params->lat), atof(params->lon), atof(params->alt));
        ble_send((uint8_t *)buf, strlen(buf), false);
    }
}

static void ss_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    flash_params_erase();
    flash_params_write(params);

    ble_send("Save Complete!\n\r", strlen("Save Complete!\n\r"), false);
    vTaskDelay(pdMS_TO_TICKS(100));
    NVIC_SystemReset();
}

static void gd_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    char device_name[40];

    sprintf(device_name, "Get %s\n\r", params->ble_device_name);
    BLE_AT_RESP_SEND(device_name);
}
static void gi_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    char id[40];

    sprintf(id, "Get %s\n\r", params->ntrip_id);
    BLE_AT_RESP_SEND(id);
}
static void gp_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    char pw[40];

    sprintf(pw, "Get %s\n\r", params->ntrip_pw);
    BLE_AT_RESP_SEND(pw);
}
static void gg_handler(ble_instance_t *inst, const char *param)
{
    user_params_t *params = flash_params_get_current();
    char loc[70];

    sprintf(loc, "Get %s,%s,%s\n\r", params->lat, params->lon, params->alt);
    BLE_AT_RESP_SEND(loc);
}
static void rs_handler(ble_instance_t *inst, const char *param)
{
    ble_get_handle()->ops->send("Device Reset\n", strlen("Device Reset\n"));
    vTaskDelay(pdMS_TO_TICKS(100));
    NVIC_SystemReset();
}
