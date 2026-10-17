#include "motor_profiles.h"
#include "motor_control.h"
#include "ihm07m1.h"
#include <string.h>

static MotorProfile_t s_profiles[MOTOR_PROFILE_COUNT] = {
    {
        .name = "Generic 7-Pole BLDC",
        .pole_pairs = 7,
        .rated_voltage_v = 12.0f,
        .max_continuous_amps = 1.20f,
        .hw_break_trip_amps = 2.20f,
        .min_rpm = 300.0f,
        .max_rpm = 3000.0f,
        .base_duty_pct = 20.0f,
        .vf_slope_pct = 26.0f,
        .accel_slew_rpm_s = 200.0f,
        .phase_resistance_ohm = 0.85f,
        .kv_rating = 920.0f,
        .characterized = true
    },
    {
        .name = "Gimbal 14-Pole BLDC",
        .pole_pairs = 14,
        .rated_voltage_v = 12.0f,
        .max_continuous_amps = 0.80f,
        .hw_break_trip_amps = 1.60f,
        .min_rpm = 150.0f,
        .max_rpm = 1500.0f,
        .base_duty_pct = 35.0f,
        .vf_slope_pct = 25.0f,
        .accel_slew_rpm_s = 80.0f,
        .phase_resistance_ohm = 5.20f,
        .kv_rating = 210.0f,
        .characterized = false
    },
    {
        .name = "Drone 7-Pole High-KV",
        .pole_pairs = 7,
        .rated_voltage_v = 12.0f,
        .max_continuous_amps = 1.40f,
        .hw_break_trip_amps = 2.40f,
        .min_rpm = 500.0f,
        .max_rpm = 4500.0f,
        .base_duty_pct = 18.0f,
        .vf_slope_pct = 40.0f,
        .accel_slew_rpm_s = 300.0f,
        .phase_resistance_ohm = 0.22f,
        .kv_rating = 1800.0f,
        .characterized = false
    },
    {
        .name = "Custom / Characterized",
        .pole_pairs = 7,
        .rated_voltage_v = 12.0f,
        .max_continuous_amps = 1.20f,
        .hw_break_trip_amps = 2.20f,
        .min_rpm = 300.0f,
        .max_rpm = 3000.0f,
        .base_duty_pct = 28.0f,
        .vf_slope_pct = 32.0f,
        .accel_slew_rpm_s = 150.0f,
        .phase_resistance_ohm = 0.0f,
        .kv_rating = 0.0f,
        .characterized = false
    }
};

static uint8_t s_active_profile_idx = 0;

void MotorProfiles_Init(void) {
    s_active_profile_idx = 0;
    MotorProfiles_ApplyActiveToDrive();
}

uint8_t MotorProfiles_GetCount(void) {
    return MOTOR_PROFILE_COUNT;
}

const MotorProfile_t* MotorProfiles_Get(uint8_t index) {
    if (index >= MOTOR_PROFILE_COUNT) return NULL;
    return &s_profiles[index];
}

uint8_t MotorProfiles_GetActiveIndex(void) {
    return s_active_profile_idx;
}

MotorProfile_t* MotorProfiles_GetActive(void) {
    return &s_profiles[s_active_profile_idx];
}

bool MotorProfiles_Select(uint8_t index) {
    if (index >= MOTOR_PROFILE_COUNT) return false;
    s_active_profile_idx = index;
    MotorProfiles_ApplyActiveToDrive();
    return true;
}

bool MotorProfiles_Next(void) {
    uint8_t next = (s_active_profile_idx + 1) % MOTOR_PROFILE_COUNT;
    return MotorProfiles_Select(next);
}

void MotorProfiles_ApplyActiveToDrive(void) {
    const MotorProfile_t* prof = &s_profiles[s_active_profile_idx];
    Motor_ApplyProfile(prof);
}

void MotorProfiles_SaveCharacterized(float rs_ohm, float kv, float base_duty, float vf_slope) {
    MotorProfile_t* custom = &s_profiles[3]; /* Profile 3 is Custom */
    strncpy(custom->name, "Custom (Characterized)", sizeof(custom->name) - 1);
    custom->phase_resistance_ohm = rs_ohm;
    custom->kv_rating = kv;
    custom->base_duty_pct = base_duty;
    custom->vf_slope_pct = vf_slope;
    custom->characterized = true;

    /* Select custom profile and update drive */
    MotorProfiles_Select(3);
}
