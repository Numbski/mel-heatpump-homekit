#include <HeatPump.h>
#include <homekit/characteristics.h>
#include <Ticker.h>

#include "accessory.h"
#include "debug.h"
#include "heatpump_client.h"

HeatPump heatpump;
Ticker updateTicker;
Ticker syncTicker;

#define HK_SPEED(s) ((float)s * 20)
#define HP_SPEED(s) (s <= 20 ? "QUIET" : s <= 40 ? "1" : s <= 60 ? "2" : s <= 80 ? "3" : "4")

// audo mode doesn't report the fan speed, we set a default to have a
// consistent value so HomeKit can properly detect when scenes are active
#define AUTO_FAN_SPEED 1

// throttle updates to the heat pump to try to send more settings at once and
// avoid conflicts when changing multiple settings from HomeKit
#define UPDATE_INTERVAL 5

heatpumpSettings _settingsForCurrentState() {
    heatpumpSettings settings;

    uint8_t therm_mode = ch_thermostat_target_heating_cooling_state.value.uint8_value;
    uint8_t fan_active = ch_fan_active.value.uint8_value;
    uint8_t dehum_active = ch_dehumidifier_active.value.uint8_value;

    settings.temperature = ch_thermostat_target_temperature.value.float_value;

    // I don't want to override, I want to keep what was there by default
    settings.mode = "AUTO";

    if (therm_mode == HOMEKIT_TARGET_HEATING_COOLING_STATE_OFF && !fan_active && !dehum_active) {
        settings.power = "OFF";
    } else {
        settings.power = "ON";
        if (therm_mode != HOMEKIT_TARGET_HEATING_COOLING_STATE_OFF) {
            if (therm_mode == HOMEKIT_TARGET_HEATING_COOLING_STATE_HEAT) {
                settings.mode = "HEAT";
            } else if (therm_mode == HOMEKIT_TARGET_HEATING_COOLING_STATE_COOL) {
                settings.mode = "COOL";
            } else {
                settings.mode = "AUTO";
            }
        } else if (dehum_active) {
            settings.mode = "DRY";
        } else if (fan_active) {
            settings.mode = "FAN";
        }
    }

    float speed = ch_fan_rotation_speed.value.float_value;
    if (ch_fan_target_state.value.uint8_value == 1) {
        settings.fan = "AUTO";
    } else if (speed > 0) {
        settings.fan = HP_SPEED(speed);
    }

    if (ch_fan_swing_mode.value.uint8_value == 1) {
        settings.vane = "SWING";
    } else {
        settings.vane = "AUTO";
    }

    if (ch_dehumidifier_swing_mode.value.uint8_value == 1) {
        settings.wideVane = "SWING";
    } else {
        settings.wideVane = "|";
    }

    return settings;
}

void scheduleHeatPumpUpdate() {
    updateTicker.once_scheduled(UPDATE_INTERVAL, [] {
        unsigned long start = millis();
        heatpumpSettings settings = _settingsForCurrentState();
        heatpump.setSettings(settings);
        MIE_LOG("⮕ HP updating power %s mode %s target %.1f fan %s v vane %s h vane %s",
                settings.power,
                settings.mode,
                settings.temperature,
                settings.fan,
                settings.vane,
                settings.wideVane);
        heatpump.update();
        MIE_LOG("HP update %dms", millis() -start);
    });
}

bool _set_characteristic_uint8(homekit_characteristic_t *characteristic, uint8_t value, bool notify = false) {
    if (characteristic->value.uint8_value != value) {
        characteristic->value.uint8_value = value;
        if (notify) {
            homekit_characteristic_notify(characteristic, characteristic->value);
        }
        return true;
    } else {
        return false;
    }
}

bool _set_characteristic_float(homekit_characteristic_t *characteristic, float value, bool notify = false) {
    if (characteristic->value.float_value != value) {
        characteristic->value.float_value = value;
        if (notify) {
            homekit_characteristic_notify(characteristic, characteristic->value);
        }
        return true;
    } else {
        return false;
    }
}


// --- Settings changes
void updateThermostatSettings(heatpumpSettings settings) {
    uint8_t state = HOMEKIT_TARGET_HEATING_COOLING_STATE_OFF;

    if (strncmp(settings.power, "OFF", 3) == 0) {
        state = HOMEKIT_TARGET_HEATING_COOLING_STATE_OFF;
    } else if (strncmp(settings.mode, "COOL", 4) == 0) {
        state = HOMEKIT_TARGET_HEATING_COOLING_STATE_COOL;
    } else if (strncmp(settings.mode, "HEAT", 4) == 0) {
        state = HOMEKIT_TARGET_HEATING_COOLING_STATE_HEAT;
    } else if (strncmp(settings.mode, "AUTO", 4) == 0) {
        state = HOMEKIT_TARGET_HEATING_COOLING_STATE_AUTO;
    } else if (strncmp(settings.mode, "DRY", 3) == 0) {
        state = HOMEKIT_TARGET_HEATING_COOLING_STATE_OFF;
    } else if (strncmp(settings.mode, "FAN", 3) == 0) {
        state = HOMEKIT_TARGET_HEATING_COOLING_STATE_OFF;
    }

    bool changed = false;
    changed |= _set_characteristic_uint8(&ch_thermostat_target_heating_cooling_state, state, true);
    changed |= _set_characteristic_float(&ch_thermostat_target_temperature, settings.temperature, true);

    if (changed) {
        MIE_LOG(" ⮕ HK therm target mode %d temp %.1f",
                ch_thermostat_target_heating_cooling_state.value.uint8_value,
                ch_thermostat_target_temperature.value.float_value);
    }
}

void updateFanSettings(heatpumpSettings settings) {
    int speed = 0;
    uint8_t targetState = FAN_TARGET_STATE_MANUAL;

    if (strncmp(settings.fan, "QUIET", 5) == 0) {
        speed = 1;
    } else if (strncmp(settings.fan, "AUTO", 4) == 0) {
        targetState = FAN_TARGET_STATE_AUTO;
        speed = AUTO_FAN_SPEED;
    } else {
        speed = 1 + strtol(settings.fan, NULL, 10);
    }

    bool changed = false;
    changed |= _set_characteristic_uint8(&ch_fan_active, (heatpump.getPowerSettingBool() ? 1 : 0), true);
    changed |= _set_characteristic_float(&ch_fan_rotation_speed, HK_SPEED(speed), true);
    changed |= _set_characteristic_uint8(&ch_fan_target_state, targetState, true);

    // vertical swing
    if (strncmp(settings.vane, "SWING", 5) == 0) {
        changed |= _set_characteristic_uint8(&ch_fan_swing_mode, 1, true);
    } else {
        changed |= _set_characteristic_uint8(&ch_fan_swing_mode, 0, true);
    }

    if (changed) {
        MIE_LOG(" ⮕ HK fan active %d speed %d auto %d swing %d",
                ch_fan_active.value.uint8_value,
                (int)ch_fan_rotation_speed.value.float_value,
                ch_fan_target_state.value.uint8_value,
                ch_fan_swing_mode.value.uint8_value);
    }
}

void updateDehumidifierSettings(heatpumpSettings settings) {
    uint8_t active = 0;
    if (strncmp(settings.mode, "DRY", 3) == 0) {
        active = heatpump.getPowerSettingBool() ? 1 : 0;
    }

    bool changed = false;
    changed |= _set_characteristic_uint8(&ch_dehumidifier_active, active);

    // horizontal swing
    if (strncmp(settings.wideVane, "SWING", 5) == 0) {
        changed |= _set_characteristic_uint8(&ch_dehumidifier_swing_mode, 1, true);
    } else {
        changed |= _set_characteristic_uint8(&ch_dehumidifier_swing_mode, 0, true);
    }

    if (changed) {
        MIE_LOG(" ⮕ HK dehum active %d swing %d",
                ch_dehumidifier_active.value.uint8_value,
                ch_dehumidifier_swing_mode.value.uint8_value);
    }
}


// --- Status updates
void updateThermostatOperatingStatus(bool operating) {
    uint8_t target_state = ch_thermostat_target_heating_cooling_state.value.uint8_value;
    float target_temperature = ch_thermostat_target_temperature.value.float_value;
    float current_temperature = ch_thermostat_current_temperature.value.float_value;

    uint8_t current_state = HOMEKIT_CURRENT_HEATING_COOLING_STATE_OFF;
    String mode = "idle";
    if (heatpump.getPowerSettingBool() && operating) {
        if (target_state == HOMEKIT_TARGET_HEATING_COOLING_STATE_HEAT) {
            current_state = HOMEKIT_CURRENT_HEATING_COOLING_STATE_HEAT;
            mode = PSTR("heating");
        } else if (target_state == HOMEKIT_TARGET_HEATING_COOLING_STATE_COOL) {
            current_state = HOMEKIT_CURRENT_HEATING_COOLING_STATE_COOL;
            mode = PSTR("cooling");
        } else if (target_state == HOMEKIT_TARGET_HEATING_COOLING_STATE_AUTO) {
            if (current_temperature < target_temperature) {
                current_state = HOMEKIT_CURRENT_HEATING_COOLING_STATE_HEAT;
                mode = PSTR("heating");
            } else if (current_temperature > target_temperature) {
                current_state = HOMEKIT_CURRENT_HEATING_COOLING_STATE_COOL;
                mode = PSTR("cooling");
            }
        }
    }

    bool changed = false;
    changed |= _set_characteristic_uint8(&ch_thermostat_current_heating_cooling_state, current_state, true);

    if (changed) {
        MIE_LOG(" ⮕ HK therm %s temp %.1f", mode.c_str(), current_temperature);
    }
}

void updateFanOperatingStatus(bool operating) {
    bool changed = false;
    String status;
    if (heatpump.getPowerSettingBool() == false) {
        changed |= _set_characteristic_uint8(&ch_fan_current_state, FAN_CURRENT_STATE_INACTIVE, true);
        status = PSTR("inactive");
    } else if (operating) {
        changed |= _set_characteristic_uint8(&ch_fan_current_state, FAN_CURRENT_STATE_BLOWING, true);
        status = PSTR("blowing");
    } else {
        changed |= _set_characteristic_uint8(&ch_fan_current_state, FAN_CURRENT_STATE_IDLE, true);
        status = PSTR("idle");
    }

    if (changed) {
        MIE_LOG(" ⮕ HK fan %s", status.c_str());
    }
}

void updateDehumidifierOperatingStatus(bool operating) {
    bool changed = false;
    if (strncmp(heatpump.getModeSetting(), "DRY", 3) == 0) {
        if (heatpump.getPowerSettingBool() == false) {
            changed |= _set_characteristic_uint8(&ch_dehumidifier_current_state, DEHUMIDIFIER_INACTIVE);
        } else if (operating) {
            changed |= _set_characteristic_uint8(&ch_dehumidifier_current_state, DEHUMIDIFIER_DEHUMIDIFYING);
        } else {
            changed |= _set_characteristic_uint8(&ch_dehumidifier_current_state, DEHUMIDIFIER_IDLE);
        }
    } else {
        changed |= _set_characteristic_uint8(&ch_dehumidifier_current_state, DEHUMIDIFIER_INACTIVE);
    }

    if (changed) {
        MIE_LOG(" ⮕ HK dehum state %d", ch_dehumidifier_current_state.value.uint8_value);
    }
}


void settingsChanged() {
    heatpumpSettings settings = heatpump.getSettings();
    MIE_LOG("⬅ HP power %s mode %s target %.1f fan %s v vane %s h vane %s",
            settings.power,
            settings.mode,
            settings.temperature,
            settings.fan,
            settings.vane,
            settings.wideVane);

    updateThermostatSettings(settings);
    updateFanSettings(settings);
    updateDehumidifierSettings(settings);
}

void statusChanged(heatpumpStatus status) {
    MIE_LOG("⬅ HP room temp %.1f op %d cmp %d",
            status.roomTemperature,
            status.operating,
            status.compressorFrequency);

    _set_characteristic_float(&ch_thermostat_current_temperature, status.roomTemperature);

    updateThermostatOperatingStatus(status.operating);
    updateFanOperatingStatus(status.operating);
    updateDehumidifierOperatingStatus(status.operating);
}


// --- HomeKit controls
void _updateFanState(bool active) {
    _set_characteristic_uint8(&ch_fan_active, active, true);
    if (active) {
        uint8_t mode = ch_fan_target_state.value.uint8_value;
        float speed = ch_fan_rotation_speed.value.float_value;
        if (mode == 1) {
            _set_characteristic_float(&ch_fan_rotation_speed, HK_SPEED(AUTO_FAN_SPEED), true);
        } else if (speed < 20) {
            _set_characteristic_uint8(&ch_fan_target_state, 1, true);
            _set_characteristic_float(&ch_fan_rotation_speed, HK_SPEED(AUTO_FAN_SPEED), true);
        }
    } else {
        // _set_characteristic_float(&ch_fan_rotation_speed, 0, true);
    }
}

void set_target_heating_cooling_state(homekit_value_t value) {
    uint8_t targetState = value.uint8_value;
    _set_characteristic_uint8(&ch_thermostat_target_heating_cooling_state, targetState);
    MIE_LOG("⬅ HK therm target state %d", targetState);

    bool active = targetState != HOMEKIT_TARGET_HEATING_COOLING_STATE_OFF;
    _updateFanState(active);
    if (active) {
        _set_characteristic_uint8(&ch_dehumidifier_active, 0, true);
    }

    scheduleHeatPumpUpdate();
}

void set_target_temperature(homekit_value_t value) {
    float targetTemperature = value.float_value;
    _set_characteristic_float(&ch_thermostat_target_temperature, targetTemperature);
    MIE_LOG("⬅ HK target temp %.1f", targetTemperature);

    scheduleHeatPumpUpdate();
}

void set_dehumidifier_active(homekit_value_t value) {
    uint8_t active = value.uint8_value;
    _set_characteristic_uint8(&ch_dehumidifier_active, active);
    MIE_LOG("⬅ HK dehum active %d", active);

    _updateFanState(active);
    if (active) {
        _set_characteristic_uint8(&ch_thermostat_target_heating_cooling_state,
                HOMEKIT_TARGET_HEATING_COOLING_STATE_OFF, true);
    }

    scheduleHeatPumpUpdate();
}

void set_swing_horizontal(homekit_value_t value) {
    uint8_t swing = value.uint8_value;
    _set_characteristic_uint8(&ch_dehumidifier_swing_mode, swing);
    MIE_LOG("⬅ HK hor swing %d", swing);

    scheduleHeatPumpUpdate();
}

void set_fan_active(homekit_value_t value) {
    uint8_t active = value.uint8_value;
    _set_characteristic_uint8(&ch_fan_active, active);
    MIE_LOG("⬅ HK fan active %d", active);

    if (!active) {
        _set_characteristic_uint8(&ch_dehumidifier_active, false, true);
        _set_characteristic_uint8(&ch_thermostat_target_heating_cooling_state,
                HOMEKIT_TARGET_HEATING_COOLING_STATE_OFF, true);
    }

    scheduleHeatPumpUpdate();
}

void set_fan_speed(homekit_value_t value) {
    float speed = roundf(value.float_value);
    if (_set_characteristic_float(&ch_fan_rotation_speed, speed)) {
        MIE_LOG("⬅ HK fan speed %d", (int)speed);

        bool active = ch_fan_active.value.uint8_value;
        uint8_t mode = ch_fan_target_state.value.uint8_value;
        if (active && mode == 1) {
            MIE_LOG(" Fan is in auto mode, ignoring speed change");
            _set_characteristic_float(&ch_fan_rotation_speed, HK_SPEED(AUTO_FAN_SPEED), true);
        } else {
            _set_characteristic_uint8(&ch_fan_target_state, 0, true);
        }

        scheduleHeatPumpUpdate();
    }
}

void set_fan_auto_mode(homekit_value_t value) {
    uint8_t mode = value.uint8_value;
    _set_characteristic_uint8(&ch_fan_target_state, mode);
    MIE_LOG("⬅ HK fan auto %d", mode);

    if (mode == 1) {
        _set_characteristic_float(&ch_fan_rotation_speed, HK_SPEED(AUTO_FAN_SPEED), true);
    }

    scheduleHeatPumpUpdate();
}

void set_fan_swing(homekit_value_t value) {
    uint8_t swing = value.uint8_value;
    _set_characteristic_uint8(&ch_fan_swing_mode, swing);
    MIE_LOG("⬅ HK ver swing %d", swing);
    scheduleHeatPumpUpdate();
}

bool setupHeatPump() {
    ch_thermostat_target_heating_cooling_state.setter = set_target_heating_cooling_state;
    ch_thermostat_target_temperature.setter = set_target_temperature;

    ch_dehumidifier_active.setter = set_dehumidifier_active;
    ch_dehumidifier_swing_mode.setter = set_swing_horizontal;

    ch_fan_active.setter = set_fan_active;
    ch_fan_rotation_speed.setter = set_fan_speed;
    ch_fan_target_state.setter = set_fan_auto_mode;
    ch_fan_swing_mode.setter = set_fan_swing;

    heatpump.setSettingsChangedCallback(settingsChanged);
    heatpump.setStatusChangedCallback(statusChanged);

    heatpump.enableExternalUpdate();
    heatpump.disableAutoUpdate();

    syncTicker.attach_ms_scheduled(500, [] {
        unsigned long start = millis();
        if (heatpump.isConnected()) {
            heatpump.sync();
            int diff = millis() - start;
            if (diff > 150) {
                MIE_LOG("HP sync %dms", diff);
            }
        }
    });

    return heatpump.connect(&Serial);
}
