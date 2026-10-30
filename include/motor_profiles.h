#ifndef __MOTOR_PROFILES_H
#define __MOTOR_PROFILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define MOTOR_PROFILE_MAX_NAME_LEN    28
#define MOTOR_PROFILE_COUNT           4

typedef struct {
    char name[MOTOR_PROFILE_MAX_NAME_LEN]; /* Motor description string */
    uint8_t pole_pairs;                   /* Magnetic pole pairs (e.g. 7, 14, 4) */
    float rated_voltage_v;                 /* Rated nominal bus voltage (e.g. 12.0 V) */
    float max_continuous_amps;             /* Safe continuous current ceiling (0.3 A to 1.4 A) */
    float hw_break_trip_amps;              /* Hardware BKIN trip limit (default: 2.2 A) */
    float min_rpm;                         /* Minimum RPM (default: 300 RPM) */
    float max_rpm;                         /* Maximum RPM (default: 3000 RPM) */
    float base_duty_pct;                   /* Baseline V/F duty at min_rpm (e.g. 28.0%) */
    float vf_slope_pct;                    /* Back-EMF compensation slope across speed range (e.g. 32.0%) */
    float accel_slew_rpm_s;                /* Acceleration slew rate limit in RPM/second (e.g. 500.0) */
    float decel_slew_rpm_s;                /* Deceleration slew rate limit in RPM/second (e.g. 400.0) */

    /* Measured / Characterized parameters */
    float phase_resistance_ohm;            /* Phase-to-neutral resistance Rs in Ohms */
    float kv_rating;                       /* Motor velocity constant Kv (RPM / Volt) */
    float load_comp_v_per_amp;             /* Active IR load compensation gain (Volts / Ampere) */
    float eddy_damping_ma_rpm;             /* Characterized eddy current brake damping (mA / RPM) */
    bool characterized;                    /* True if parameters were experimentally measured */
} MotorProfile_t;

void MotorProfiles_Init(void);
uint8_t MotorProfiles_GetCount(void);
const MotorProfile_t* MotorProfiles_Get(uint8_t index);
uint8_t MotorProfiles_GetActiveIndex(void);
MotorProfile_t* MotorProfiles_GetActive(void);
bool MotorProfiles_Select(uint8_t index);
bool MotorProfiles_Next(void);
void MotorProfiles_ApplyActiveToDrive(void);
void MotorProfiles_SaveCharacterized(float rs_ohm, float kv, float base_duty, float vf_slope,
                                    float load_comp, float eddy_damping);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_PROFILES_H */
