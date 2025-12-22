#include "rs485_cmd.h"
#include "board_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "timers.h"
#include "semphr.h"
#include "flash_params.h"
#include "gps_app.h"
#include "lora_app.h"
#include "gsm_app.h"
#include "gsm.h"
#include "lte_init.h"
#include "rs485_app.h"

#ifndef TAG
#define TAG "RS485_CMD"
#endif

#include "log.h"

static const char *rs485_resp_str_ok = "OK\r";
static const char *rs485_resp_str_invalid = "+E01\r";
static const char *rs485_resp_str_param_err = "+E02\r";
static const char *rs485_resp_str_not_rdy = "+E03\r";
static const char *rs485_resp_str_err = "+ERROR\r";

#define RS485_AT_RESP_SEND(data) rs485_send(data, strlen(data))
#define RS485_AT_RESP_SEND_OK() RS485_AT_RESP_SEND(rs485_resp_str_ok)
#define RS485_AT_RESP_SEND_INVALID() RS485_AT_RESP_SEND(rs485_resp_str_invalid)
#define RS485_AT_RESP_SEND_PARAM_ERR() RS485_AT_RESP_SEND(rs485_resp_str_param_err)
#define RS485_AT_RESP_SEND_NOT_RDY() RS485_AT_RESP_SEND(rs485_resp_str_not_rdy)
#define RS485_AT_RESP_SEND_ERR() RS485_AT_RESP_SEND(rs485_resp_str_err)

static void at_handler(const char *param);
static void atz_handler(const char *param);
static void atandz_handler(const char *param);
static void at_ver_handler(const char *param);
static void at_gps_manuf_handler(const char *param);
static void at_read_config_handler(const char *param);
static void at_set_baseline_handler(const char *param);
static void at_set_ntrip_ip_handler(const char *param);
static void at_set_ntrip_id_handler(const char *param);
static void at_set_ntrip_mountpoint_handler(const char *param);
static void at_set_ntrip_passwd_handler(const char *param);
static void at_set_rtk_start_handler(const char *param);
static void at_set_rtk_stop_handler(const char *param);
static void at_save_handler(const char *param);

static const at_cmd_entry_t at_cmd_table[] = {
	    {"AT+GPSMANUF?", at_gps_manuf_handler},
	    {"AT+CONFIG?", at_read_config_handler},
	    {"AT+SETBASELINE:", at_set_baseline_handler},
	    {"AT+GUGUSTART:", at_set_rtk_start_handler},
	    {"AT+GUGUSTOP", at_set_rtk_stop_handler},
	    {"AT+CASTER:", at_set_ntrip_ip_handler},
	    {"AT+MOUNTPOINT=", at_set_ntrip_mountpoint_handler},
	    {"AT+PASSWD=", at_set_ntrip_passwd_handler},
	    {"AT+VER?", at_ver_handler},
	    {"AT+ID=", at_set_ntrip_id_handler},
      {"AT+SAVE", at_save_handler},
	    {"AT&F", atandz_handler},
	    {"ATZ", atz_handler},
	    {"AT", at_handler},
    {NULL, NULL}};

volatile bool base_init_finish = false;
volatile bool is_gugu_start = false;
rtk_active_status_t active_status = RTK_ACTIVE_STATUS_NONE;

static void base_init_complete(bool success, void *user_data)
{
  gps_id_t id = (gps_id_t)(uintptr_t)user_data;
  LOG_INFO("GPS[%d] Fix mode init %s", id, success ? "succeeded" : "failed");

  base_init_finish = true;
}

void rs485_at_cmd_handler(rs485_instance_t *inst)
{
    for (int i = 0; at_cmd_table[i].name != NULL; i++)
    {
        size_t name_len = strlen(at_cmd_table[i].name);

        if (strncmp(inst->parser.data, at_cmd_table[i].name, name_len) == 0)
        {
            at_cmd_table[i].handler(inst->parser.data + name_len);
            return;
        }
    }
}

static void at_handler(const char *param)
{
    RS485_AT_RESP_SEND_OK();
}

static void atz_handler(const char *param)
{
    // xSemaphoreTake(rs485_get_instance()->mutex, portMAX_DELAY);
    rs485_get_handle()->ops->tx_enable();
    rs485_get_handle()->ops->send("+RESET\r", strlen("+RESET\r"));
    vTaskDelay(pdMS_TO_TICKS(2));
    rs485_get_handle()->ops->rx_enable();
    // xSemaphoreGive(rs485_get_instance()->mutex);

    vTaskDelay(pdMS_TO_TICKS(200));

    HAL_NVIC_SystemReset();
}

static void atandz_handler(const char *param)
{
    if(flash_params_erase() == HAL_OK)
    {
        rs485_get_handle()->ops->tx_enable();
        rs485_get_handle()->ops->send("+CONFIGINIT\r", strlen("+CONFIGINIT\r"));
        vTaskDelay(pdMS_TO_TICKS(2));
        rs485_get_handle()->ops->rx_enable();

        vTaskDelay(pdMS_TO_TICKS(200));
        NVIC_SystemReset();
    }
    else
    {
        RS485_AT_RESP_SEND_ERR();
    }
}

static void at_ver_handler(const char *param)
{
    char version_str[20];
    sprintf(version_str, "%s\r", BOARD_VERSION);
    RS485_AT_RESP_SEND(version_str);
}

static void at_gps_manuf_handler(const char *param)
{
    const board_config_t *config = board_get_config();
    char manuf_str[20];

    if (config->board == BOARD_TYPE_BASE_F9P || config->board == BOARD_TYPE_ROVER_F9P)
    {
        sprintf(manuf_str, "+Ublox\r");
    }
    else if (config->board == BOARD_TYPE_BASE_UM982 || config->board == BOARD_TYPE_ROVER_UM982)
    {
        sprintf(manuf_str, "+Unicore\r");
    }
    else
    {
        RS485_AT_RESP_SEND_ERR();
        return;
    }

    RS485_AT_RESP_SEND(manuf_str);
}

static void at_read_config_handler(const char *param)
{
    user_params_t *params = flash_params_get_current();

    char resp_str[256];

    sprintf(resp_str,
             "+CONFIG=%s,%s,%s,%s,%s,%s,%s,%s,%s,%lf,%s\r",
             params->ntrip_url,
             params->ntrip_port,
             params->ntrip_id,
             params->ntrip_pw,
             params->ntrip_mountpoint,
             params->use_manual_position ? "MANUAL" : "AUTO",
             params->lat,
             params->lon,
             params->alt,
             params->baseline_len,
             params->ble_device_name);

    RS485_AT_RESP_SEND(resp_str);
}

static void at_set_baseline_handler(const char *param)
{
    float baseline_value = strtof(param, NULL);

      if (baseline_value != 0)
      {
        flash_params_set_baseline_len(baseline_value);
        gps_set_heading_length();

        RS485_AT_RESP_SEND_OK();
      }
      else
      {
        RS485_AT_RESP_SEND_PARAM_ERR();
      }
}

static void at_set_ntrip_ip_handler(const char *param)
{
    user_params_t *params = flash_params_get_current();
    char buf[72];

    char caster_host[64];
    char caster_port[8];

    char *port_sep = strrchr(param, ':'); // 마지막 ':' 찾기
      if (port_sep)
      {
        *port_sep = '\0'; // host와 port 분리
        strncpy(caster_host, param, sizeof(caster_host) - 1);

        char *end = strchr(port_sep + 1, '\r');
        if (end)
          *end = '\0';
        strncpy(caster_port, port_sep + 1, sizeof(caster_port) - 1);
      }

      int host_len = strlen(caster_host);
      int port_len = strlen(caster_port);

    if (host_len == 0 || port_len == 0 || host_len >= 63 || port_len >= 7)
    {
        RS485_AT_RESP_SEND_PARAM_ERR();
    }
    else
    {
        flash_params_set_ntrip_url((const char *)caster_host);
        flash_params_set_ntrip_port((const char *)caster_port);

        sprintf(buf, "+CASTER=%s:%s\r", params->ntrip_url, params->ntrip_port);
        RS485_AT_RESP_SEND(buf);
    }
}

static void at_set_ntrip_id_handler(const char *param)
{
    user_params_t *params = flash_params_get_current();
    char buf[64];
    char ntrip_id[32];
      char *end = strchr(param, '\r');
      if (end)
        *end = '\0';
      strncpy(ntrip_id, param, sizeof(ntrip_id) - 1);

      int id_len = strlen(ntrip_id);
      if (id_len == 0 || id_len >= 31)
      {
        RS485_AT_RESP_SEND_PARAM_ERR();
      }
      else
      {
        flash_params_set_ntrip_id(ntrip_id);

        sprintf(buf, "+ID=%s\r", params->ntrip_id);
        RS485_AT_RESP_SEND(buf);
      }
}

static void at_set_ntrip_mountpoint_handler(const char *param)
{
    user_params_t *params = flash_params_get_current();
    char buf[64];

    flash_params_set_ntrip_mountpoint(param);
    sprintf(buf, "+MOUNTPOINT=%s\r", params->ntrip_mountpoint);
    RS485_AT_RESP_SEND(buf);
}

static void at_set_ntrip_passwd_handler(const char *param)
{
    user_params_t *params = flash_params_get_current();
    char passwd[32];
    char buf[64];

    flash_params_set_ntrip_pw(passwd);
    sprintf(buf, "+PASSWORD=%s\r", params->ntrip_pw);
    RS485_AT_RESP_SEND(passwd);
}

static void at_set_rtk_start_handler(const char *param)
{
    if (strncmp(param, "LTE", 3) == 0)
      {
        if(active_status == RTK_ACTIVE_STATUS_LORA)
        {
          lora_instance_deinit();
        }
        
        gps_init_all();
        gsm_start_rover();
        rs485_send("+GUGUSTART-LTE", strlen("+GUGUSTART-LTE"));
        active_status = RTK_ACTIVE_STATUS_GSM;
        is_gugu_start = true;

        if (gps_send_timer != NULL)
        {
          xTimerStart(gps_send_timer, 0);
        }
      }
      else if (strncmp(param, "LORA", 4) == 0)
      {
        if(active_status == RTK_ACTIVE_STATUS_GSM)
        {
          ntrip_stop();
          vTaskDelay(pdMS_TO_TICKS(100));
          gsm_port_set_airplane_mode(true);
        }
        
        gps_init_all();
        lora_start_rover();
        rs485_send("+GUGUSTART-LORA", strlen("+GUGUSTART-LORA"));
        active_status = RTK_ACTIVE_STATUS_LORA;
        is_gugu_start = true;

        if (gps_send_timer != NULL)
        {
          xTimerStart(gps_send_timer, 0);
        }
      }
      else
      {
        RS485_AT_RESP_SEND_PARAM_ERR();
      }
}

static void at_set_rtk_stop_handler(const char *param)
{
       if(active_status == RTK_ACTIVE_STATUS_LORA)
      {
        lora_instance_deinit();
        gps_cleanup_all();
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms 대기 권장
        is_gugu_start = false;
        active_status = RTK_ACTIVE_STATUS_NONE;
        rs485_send("+GUGUSTOP\r", strlen("+GUGUSTOP\r"));
      }
      else if(active_status == RTK_ACTIVE_STATUS_GSM)
      {
        if(lte_get_init_state() == LTE_INIT_DONE)
        {
          // gsm 초기화
          if (gps_send_timer != NULL)
          {
              xTimerStop(gps_send_timer, 0);
          }
          is_gugu_start = false;
          ntrip_stop();
          vTaskDelay(pdMS_TO_TICKS(100));
          gsm_port_set_airplane_mode(true);

          gps_cleanup_all();
          vTaskDelay(pdMS_TO_TICKS(100));  // 100ms 대기 권장;
          active_status = RTK_ACTIVE_STATUS_NONE;
          rs485_send("+GUGUSTOP\r", strlen("+GUGUSTOP\r"));

        }
        else
        {
            RS485_AT_RESP_SEND_NOT_RDY();
        }
      }
      else
      {
        is_gugu_start = false;
        rs485_send("+GUGUSTOP\r", strlen("+GUGUSTOP\r"));
      }
}

static void at_save_handler(const char *param)
{
    user_params_t *params = flash_params_get_current();
      if (flash_params_erase() != HAL_OK)
      {
        RS485_AT_RESP_SEND_ERR();
      }
      else
      {
        if (flash_params_write(params) != HAL_OK)
        {
          RS485_AT_RESP_SEND_ERR();
        }
        else
        {
        rs485_send((uint8_t *)"+SAVE\r", strlen("+SAVE\r"));
        vTaskDelay(pdMS_TO_TICKS(500));
          NVIC_SystemReset();
        }
      }
}
