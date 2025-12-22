#ifndef FLASH_PARAMS_H
#define FLASH_PARAMS_H

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_flash.h"

typedef struct
{
    uint32_t magic;

    char ntrip_url[64];
    char ntrip_port[8];
    char ntrip_id[32];
    char ntrip_pw[32];
    char ntrip_mountpoint[32];
    
    uint32_t boot_lte;
    uint32_t boot_lora;
    uint32_t use_manual_position;
    char lat[16];
    char lon[16];
    char alt[16];

    float baseline_len;

    char ble_device_name[32];

    uint32_t base_auto_fix_enabled;
}user_params_t;

HAL_StatusTypeDef flash_params_erase(void);
HAL_StatusTypeDef flash_params_read(user_params_t *params);
HAL_StatusTypeDef flash_params_write(user_params_t *params);

user_params_t* flash_params_get_current(void);
HAL_StatusTypeDef flash_params_save(user_params_t *params);
HAL_StatusTypeDef flash_params_init(void);

/* 파라미터 설정 함수 */
void flash_params_set_ntrip_url(const char* url);
void flash_params_set_ntrip_port(const char* port);
void flash_params_set_ntrip_id(const char* id);
void flash_params_set_ntrip_pw(const char* pw);
void flash_params_set_ntrip_mountpoint(const char* mountpoint);
void flash_params_set_manual_position(uint32_t use_manual, const char* lat, const char* lon, const char* alt);
void flash_params_set_baseline_len(float len);
void flash_params_set_ble_device_name(const char* name);

#endif
