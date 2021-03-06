#pragma once

#include <homekit/characteristics.h>
#include <homekit/homekit.h>
#include <homekit/types.h>

// Thermostat
extern homekit_characteristic_t ch_thermostat_current_temperature;
extern homekit_characteristic_t ch_thermostat_target_temperature;
extern homekit_characteristic_t ch_thermostat_current_heating_cooling_state;
extern homekit_characteristic_t ch_thermostat_target_heating_cooling_state;

// Dehumidifier

#define DEHUMIDIFIER_INACTIVE 0
#define DEHUMIDIFIER_IDLE 1
// #define DEHUMIDIFIER_CURRENT_STATE_HUMIDIFYING 2
#define DEHUMIDIFIER_DEHUMIDIFYING 3

extern homekit_characteristic_t ch_dehumidifier_active;
extern homekit_characteristic_t ch_dehumidifier_current_state;
extern homekit_characteristic_t ch_dehumidifier_swing_mode;
extern homekit_characteristic_t ch_dehumidifier_relative_humidity;
extern homekit_characteristic_t ch_dew_point;

// Fan

#define FAN_TARGET_STATE_MANUAL 0
#define FAN_TARGET_STATE_AUTO 1

#define FAN_CURRENT_STATE_INACTIVE 0
#define FAN_CURRENT_STATE_IDLE 1
#define FAN_CURRENT_STATE_BLOWING 2

extern homekit_characteristic_t ch_fan_active;
extern homekit_characteristic_t ch_fan_rotation_speed;
extern homekit_characteristic_t ch_fan_swing_mode;
extern homekit_characteristic_t ch_fan_current_state;
extern homekit_characteristic_t ch_fan_target_state;

// Accessory
extern homekit_characteristic_t accessory_name;
extern homekit_characteristic_t accessory_serial;
extern homekit_server_config_t accessory_config;

// Helpers
#ifdef __cplusplus
extern "C" {
#endif

extern bool accessory_set_uint8(homekit_characteristic_t *characteristic, uint8_t value, bool notify);
extern bool accessory_set_float(homekit_characteristic_t *characteristic, float value, bool notify);

#ifdef __cplusplus
}
#endif
