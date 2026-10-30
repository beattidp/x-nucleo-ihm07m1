#include "motor_profiles.h"
#include "motor_control.h"
#include "power_stage.h"
#include <string.h>

static MotorProfile_t s_profiles[MOTOR_PROFILE_COUNT] = {
    {
        /* Profile 0: Generic 12V 7-Pole Pair BLDC Motor (Baseline Bringup) */
        .name = "Generic 7-Pole BLDC",
        .pole_pairs = 7,
        .rated_voltage_v = 12.0f,
        .max_continuous_amps = 1.20f,
        .hw_break_trip_amps = 2.20f,
        .min_rpm = 300.0f,
        .max_rpm = 3000.0f,
        .base_duty_pct = 12.0f,
        .vf_slope_pct = 26.0f,
        .accel_slew_rpm_s = 150.0f,
        .decel_slew_rpm_s = 200.0f,
        .phase_resistance_ohm = 0.85f,
        .kv_rating = 920.0f,
        .load_comp_v_per_amp = 0.50f,
        .eddy_damping_ma_rpm = 0.0f,
        .characterized = true
    },
    {
        /* Profile 1: 5010 360KV Outrunner Motor - Bare Rotor (No Flywheel / Minimal Load)
         * 12N14P configuration: 12 stator slots, 14 rotor magnets (7 pole pairs).
         * Extremely low mechanical inertia (J ~ 3.5e-6 kg*m^2) and very low no-load drag (~15 mA). */
        .name = "5010-360KV Bare Rotor",
        .pole_pairs = 7,
        .rated_voltage_v = 24.0f,
        .max_continuous_amps = 1.20f,
        .hw_break_trip_amps = 2.20f,
        .min_rpm = 100.0f,
        .max_rpm = 6500.0f,
        .base_duty_pct = 12.0f,
        .vf_slope_pct = 38.0f,
        .accel_slew_rpm_s = 250.0f,  /* Fast acceleration suited for bare low-inertia rotor */
        .decel_slew_rpm_s = 250.0f,  /* Controlled deceleration preventing bench supply bus pumping */
        .phase_resistance_ohm = 0.50f,
        .kv_rating = 360.0f,
        .load_comp_v_per_amp = 0.50f,
        .eddy_damping_ma_rpm = 0.0f,
        .characterized = true
    },
    {
        /* Profile 2: High-Torque 170KV Motor (Hall Sensor Equipped)
         * Higher phase resistance and lower Kv, intended for high torque applications. */
        .name = "High-Torque 170KV",
        .pole_pairs = 7,
        .rated_voltage_v = 24.0f,
        .max_continuous_amps = 1.40f,
        .hw_break_trip_amps = 2.20f,
        .min_rpm = 100.0f,
        .max_rpm = 3000.0f,
        .base_duty_pct = 14.0f,
        .vf_slope_pct = 40.0f,
        .accel_slew_rpm_s = 40.0f,
        .decel_slew_rpm_s = 50.0f,
        .phase_resistance_ohm = 1.20f,
        .kv_rating = 170.0f,
        .load_comp_v_per_amp = 1.00f,
        .eddy_damping_ma_rpm = 0.0f,
        .characterized = false
    },
    {
        /* Profile 3 (Index 3, Displayed as [4/4]): 5010-360KV Motor with Copper Disc Flywheel & Eddy Current Brake
         * 
         * Physical Plant Dynamics:
         * 1. High Rotational Inertia: Solid copper flywheel disc attached to outrunner can.
         *    - Mass m ~= 90 g, Radius R ~= 40 mm -> J = 0.5 * m * R^2 ~= 7.2e-5 kg*m^2.
         *    - Requires controlled acceleration slew to match available motor torque without pole slipping.
         * 2. Velocity-Dependent Eddy Current Braking:
         *    - Permanent neodymium magnet mounted in close proximity to the rotating copper disc.
         *    - By Faraday's and Lenz's laws, rotating conductor in magnetic field B induces eddy currents:
         *        J_eddy = sigma * (v x B) = sigma * (omega x r) x B
         *    - The resulting Lorentz force produces an opposing braking torque proportional to velocity:
         *        tau_drag(omega) = c * B^2 * omega  (Linear viscous damping)
         * 3. Load Compensation & Governor:
         *    - Base duty = 17.5% provides 4.24V starting boost to overcome flywheel stiction and magnetic detent.
         *      Peak starting current into total loop resistance (R_LL + 2*Rdson + 2*Rshunt = 3.12 Ohm) is:
         *      I_pk = 4.24V / 3.12 Ohm = 1.36A (safely under L6230 continuous 1.4A and trip limit 2.2A).
         *    - Accel slew rate = 12.0 RPM/s matches the high angular inertia of the copper disc (J ~ 7.2e-5 kg*m^2)
         *      and physical flywheel acceleration measured under eddy current brake damping (11.4 RPM/s).
         *    - Load compensation (1.50 V/A) dynamically counteracts stator IR drop as phase current mounts.
         *    - Eddy damping feedforward (2.40 mA/RPM) progressively injects voltage:
         *        V_eddy = (RPM * 2.40 / 1000) * 3.12 Ohm (e.g. 2.25V at 300 RPM, 3.37V at 450 RPM, 4.49V at 600 RPM)
         *      to overcome Lorentz drag torque (tau_eddy = c * B^2 * omega) induced in the copper disc.
         *    - Optical closed-loop governor binds stator frequency to rotor speed (zero frequency slip, target_lead = +0.2 RPM)
         *      to maintain constant synchronous torque angle and eliminate pole-slip overcurrent tripping. */
        .name = "5010-360KV Flywheel",
        .pole_pairs = 7,
        .rated_voltage_v = 24.0f,
        .max_continuous_amps = 1.20f,
        .hw_break_trip_amps = 2.50f,
        .min_rpm = 100.0f,
        .max_rpm = 4000.0f,
        .base_duty_pct = 17.5f,      /* 17.5% base duty provides 4.24V breakaway torque (1.36A peak) */
        .vf_slope_pct = 38.0f,
        .accel_slew_rpm_s = 4.0f,    /* 4.0 RPM/s matched acceleration for copper flywheel under eddy brake */
        .decel_slew_rpm_s = 50.0f,   /* Smooth deceleration utilizing eddy current brake damping */
        .phase_resistance_ohm = 0.50f,
        .kv_rating = 360.0f,
        .load_comp_v_per_amp = 1.50f,/* 1.50 V/A active IR compensation under eddy brake drag */
        .eddy_damping_ma_rpm = 1.50f,/* 1.50 mA/RPM linear feedforward for reduced/lowered magnet */
        .characterized = false
    }
};

static uint8_t s_active_profile_idx = 1; /* Default to Profile 1: 5010-360KV Bare Rotor */

void MotorProfiles_Init(void) {
    s_active_profile_idx = 1;
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

/**
 * @brief Save experimental characterization results into Profile 3 (Custom/Characterized)
 *        and activate it immediately on the motor control drive.
 */
void MotorProfiles_SaveCharacterized(float rs_ohm, float kv, float base_duty, float vf_slope,
                                    float load_comp, float eddy_damping) {
    MotorProfile_t* custom = &s_profiles[3]; /* Profile 3 is dedicated for Characterized / Custom */
    strncpy(custom->name, "Custom (Characterized)", sizeof(custom->name) - 1);
    custom->pole_pairs = s_profiles[s_active_profile_idx].pole_pairs;
    custom->rated_voltage_v = s_profiles[s_active_profile_idx].rated_voltage_v;
    custom->max_continuous_amps = s_profiles[s_active_profile_idx].max_continuous_amps;
    custom->hw_break_trip_amps = s_profiles[s_active_profile_idx].hw_break_trip_amps;
    custom->min_rpm = s_profiles[s_active_profile_idx].min_rpm;
    custom->max_rpm = s_profiles[s_active_profile_idx].max_rpm;
    custom->accel_slew_rpm_s = 4.0f; /* Match flywheel mechanical inertia to prevent stall */
    custom->decel_slew_rpm_s = 50.0f;
    custom->phase_resistance_ohm = (rs_ohm > 0.1f && rs_ohm < 2.0f) ? rs_ohm : 0.50f;
    custom->kv_rating = kv;
    custom->base_duty_pct = 17.5f; /* 17.5% base duty provides 4.24V breakaway torque */
    custom->vf_slope_pct = vf_slope;
    custom->load_comp_v_per_amp = (load_comp < 1.20f) ? 1.50f : load_comp;
    custom->eddy_damping_ma_rpm = (eddy_damping > 0.0f) ? eddy_damping : 1.50f;
    custom->characterized = true;

    /* Select custom profile and update drive */
    MotorProfiles_Select(3);
}
