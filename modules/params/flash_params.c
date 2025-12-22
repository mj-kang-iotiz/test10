#include <string.h>
#include "flash_params.h"

#ifndef TAG
    #define TAG "FLASH_PARAMS"
#endif

#include "log.h"

#define FLASH_MAGIC_NUMBER 0xAA55AA55U
#define FLASH_USER_PARAMS_SECTOR  FLASH_SECTOR_11
#define FLASH_USER_START_ADDR    0x080E0000U


static const user_params_t user_default_params = 
{
    .magic = FLASH_MAGIC_NUMBER,
    .ntrip_url = "", // ntrip.hi-rtk.io
    .ntrip_port = "", // 2101
    .ntrip_id = "", // iotiz1
    .ntrip_pw = "", // 1234
    .ntrip_mountpoint = "", // RTK_SMT_MSG
    .use_manual_position = 0,
    .lat = "", // 37.4136149088
    .lon = "", // 127.125455729
    .alt = "", // 62.0923
    .baseline_len = 100.0,
    .ble_device_name = "GuguBase",
	.base_auto_fix_enabled = 1,
};

static user_params_t current_params;

HAL_StatusTypeDef flash_params_erase(void)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;

    HAL_FLASH_Unlock();

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = FLASH_USER_PARAMS_SECTOR;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase_init.NbSectors = 1;

    status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    if (status != HAL_OK) {
        uint32_t error_code = HAL_FLASH_GetError();
        LOG_ERR("Flash erase failed: %d error_code :%d", status, error_code);
        HAL_FLASH_Lock();
        return status;
    }

    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();

    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_INSTRUCTION_CACHE_RESET();

    __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
    __HAL_FLASH_DATA_CACHE_ENABLE();


    HAL_FLASH_Lock();
    return HAL_OK;
}

HAL_StatusTypeDef flash_params_read(user_params_t *params)
{
    if (params == NULL) {
        LOG_ERR("Invalid parameter: NULL pointer");
        return HAL_ERROR;
    }

    uint32_t magic_number = *(uint32_t*)FLASH_USER_START_ADDR;

    if(magic_number != FLASH_MAGIC_NUMBER)
    {
        LOG_WARN("Flash params not initialized, loading default params");
        memcpy(params, &user_default_params, sizeof(user_params_t));
        return HAL_OK;
    }

    // Flash 메모리에서 직접 복사
    memcpy(params, (void*)FLASH_USER_START_ADDR, sizeof(user_params_t));

    if (params->magic != FLASH_MAGIC_NUMBER) {
         LOG_WARN("Flash params not initialized or corrupted");
         return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef flash_params_write(user_params_t *params)
{
    HAL_StatusTypeDef status;
  /* Unlock to control */
  HAL_FLASH_Unlock();
  
  uint32_t addr = FLASH_USER_START_ADDR;
  uint32_t len = sizeof(user_params_t);
  uint32_t* data = (uint32_t*)params;
  uint32_t words = (sizeof(user_params_t) + 3) / 4;  // 올림
  
    for(uint32_t i = 0; i< words; i++)
    {
        /* Writing data to flash memory */
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data[i]);
        if(status == HAL_OK)
        {
        addr+=4;
        }
        else
        {
        uint32_t error_code = HAL_FLASH_GetError();
        LOG_ERR("Flash write failed: %d error_code :%d", status, error_code);
        HAL_FLASH_Lock();
        return HAL_ERROR;
        }
    }

  /* Lock flash control register */
  HAL_FLASH_Lock();  
  
  return HAL_OK;
}

user_params_t* flash_params_get_current(void)
{
    return &current_params;
}

HAL_StatusTypeDef flash_params_save(user_params_t *params)
{
    params->magic = FLASH_MAGIC_NUMBER;
    
    if (flash_params_erase() != HAL_OK) {
        return HAL_ERROR;
    }

    if (flash_params_write(params) != HAL_OK) {
        return HAL_ERROR;
    }

    user_params_t verify;
    if (flash_params_read(&verify) != HAL_OK) {
        return HAL_ERROR;
    }
    
    if (memcmp(params, &verify, sizeof(user_params_t)) != 0) {
        LOG_ERR("Flash verify failed");
        return HAL_ERROR;
    }

    memcpy(&current_params, params, sizeof(user_params_t));
    
    return HAL_OK;
}

HAL_StatusTypeDef flash_params_init(void)
{
    return flash_params_read(&current_params);
}

void flash_params_set_ntrip_url(const char *url)
{
    strncpy(current_params.ntrip_url, url, sizeof(current_params.ntrip_url) - 1);
    current_params.ntrip_url[sizeof(current_params.ntrip_url) - 1] = '\0';
}

void flash_params_set_ntrip_port(const char *port)
{
    strncpy(current_params.ntrip_port, port, sizeof(current_params.ntrip_port) - 1);
    current_params.ntrip_port[sizeof(current_params.ntrip_port) - 1] = '\0';
}

void flash_params_set_ntrip_id(const char *id)
{
    strncpy(current_params.ntrip_id, id, sizeof(current_params.ntrip_id) - 1);
    current_params.ntrip_id[sizeof(current_params.ntrip_id) - 1] = '\0';
}

void flash_params_set_ntrip_pw(const char *pw)
{
    strncpy(current_params.ntrip_pw, pw, sizeof(current_params.ntrip_pw) - 1);
    current_params.ntrip_pw[sizeof(current_params.ntrip_pw) - 1] = '\0';
}

void flash_params_set_ntrip_mountpoint(const char *mountpoint)
{
    strncpy(current_params.ntrip_mountpoint, mountpoint, sizeof(current_params.ntrip_mountpoint) - 1);
    current_params.ntrip_mountpoint[sizeof(current_params.ntrip_mountpoint) - 1] = '\0';
}

void flash_params_set_manual_position(uint32_t use_manual, const char* lat, const char* lon, const char* alt)
{
    current_params.use_manual_position = use_manual;

    if(lat)
    {
        strncpy(current_params.lat, lat, sizeof(current_params.lat) - 1);
        current_params.lat[sizeof(current_params.lat) - 1] = '\0';
    }

    if(lon)
    {
        strncpy(current_params.lon, lon, sizeof(current_params.lon) - 1);
        current_params.lon[sizeof(current_params.lon) - 1] = '\0';
    }

    if(alt)
    {
        strncpy(current_params.alt, alt, sizeof(current_params.alt) - 1);
        current_params.alt[sizeof(current_params.alt) - 1] = '\0';
    }
}

void flash_params_set_baseline_len(float len)
{
    current_params.baseline_len = len;
}

void flash_params_set_ble_device_name(const char* name)
{
    strncpy(current_params.ble_device_name, name, sizeof(current_params.ble_device_name) - 1);
    current_params.ble_device_name[sizeof(current_params.ble_device_name) - 1] = '\0';
}
