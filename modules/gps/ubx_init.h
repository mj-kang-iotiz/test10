#ifndef UBX_INIT_H
#define UBX_INIT_H

#include "gps.h"
#include <stdbool.h>

bool ubx_rover_init(gps_t* gps);
bool ubx_base_init(gps_t* gps);
bool ubx_moving_base_init(gps_t* gps);

bool ubx_factory_reset(gps_t* gps, ubx_init_complete_callback_t callback, void *user_data);

bool ubx_set_fixed_position_async(gps_t* gps, const char* lat_str, const char* lon_str,
                                   const char* alt_str, ubx_init_complete_callback_t callback,
                                   void *user_data);

bool ubx_set_survey_in_mode_async(gps_t* gps, uint32_t min_duration, uint32_t accuracy_limit,
                                   ubx_init_complete_callback_t callback, void *user_data);

#endif
