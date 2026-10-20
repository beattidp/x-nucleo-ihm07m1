#ifndef __MOTOR_CONTROL_H
#define __MOTOR_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "motor_profiles.h"

typedef enum {
    MOTOR_MODE_IDLE = 0,
    MOTOR_MODE_OPEN_LOOP_CHAR,      /* Safe rotating voltage vector for motor characterization */
    MOTOR_MODE_SIX_STEP_VOLTAGE,    /* 6-step voltage mode commutation (SPN8 logic) */
    MOTOR_MODE_FOC                  /* 4-quadrant Field Oriented Control */
} MotorMode_t;

typedef enum {
    MOTOR_STATE_STOPPED = 0,
    MOTOR_STATE_STARTING,
    MOTOR_STATE_RUNNING,
    MOTOR_STATE_BRAKING,            /* Active controlled deceleration */
    MOTOR_STATE_CHARACTERIZING,     /* Automated motor characterization running */
    MOTOR_STATE_FAULT
} MotorState_t;

typedef struct MotorTelemetry_s {
    float vbus_v;                   /* DC Bus voltage in Volts */
    float temp_c;                   /* Driver board temperature in deg C */
    float pot_pct;                  /* User speed potentiometer (0 - 100 %) */
    float pot_target_rpm;           /* Computed user potentiometer target speed in RPM */
    float current_a_amps;           /* Phase A current in Amps */
    float current_b_amps;           /* Phase B current in Amps */
    float current_c_amps;           /* Phase C current in Amps */
    float current_peak_amps;        /* Highest instantaneous phase current */
    float id_amps;                  /* Direct (flux-producing) current in Amps */
    float iq_amps;                  /* Quadrature (torque-producing) current in Amps */
    uint8_t hall_state;             /* Current Hall state (1 to 6) */
    bool fault_active;              /* L6230 DIAG line active (low) */
    float optical_rpm;              /* Measured mechanical speed from photodetector tachometer (RPM) */
    uint32_t optical_pulse_count;   /* Cumulative optical revolution pulse count */
    bool motor_stalled;             /* True if drive commanded active but no physical rotation detected */
    bool motor_slipping;            /* True if significant slip / vibration detected between cmd and real speed */
    uint32_t sample_count;          /* Telemetry sample count */
} MotorTelemetry_t;

#define MOTOR_POT_MIN_RPM           300.0f
#define MOTOR_POT_MAX_RPM           3000.0f

typedef struct MotorControl_s {
    MotorMode_t mode;
    MotorState_t state;
    int8_t direction;               /* +1 for CW, -1 for CCW */
    float target_speed_rpm;         /* Target speed in RPM */
    float actual_speed_rpm;         /* Measured or estimated RPM */
    float duty_pct;                 /* Commanded duty cycle (0.0 to 100.0%) */
    float electrical_freq_hz;       /* Commutation electrical frequency in Hz */
    uint8_t pole_pairs;             /* Motor pole pairs (default: 7) */
    float current_limit_amps;       /* Hardware trip threshold (default: 2.2 A) */
    float torque_limit_amps;        /* Software continuous current/torque ceiling (0.3 to 1.4 A) */
    float active_duty_pct;          /* Instantaneous regulated duty cycle (after V/F & foldback) */
    uint32_t run_time_ms;           /* Elapsed motor run time in ms */
    uint32_t startup_timer_ms;      /* Elapsed startup timer in ms (alignment & ramp) */
    float current_freq_hz;          /* Instantaneous electrical frequency in Hz */
    bool pot_control_enabled;       /* Whether potentiometer actively controls target speed */
    float vf_slope_pct;             /* Back-EMF compensation slope (% duty across speed range) */
    float accel_slew_rpm_s;         /* Acceleration slew rate (RPM / second) */
    float decel_slew_rpm_s;         /* Deceleration slew rate (RPM / second) */
    float min_rpm;                  /* Minimum RPM */
    float max_rpm;                  /* Maximum RPM */
    float phase_resistance_ohm;     /* Motor phase resistance in Ohms */
    float kv_rating;                /* Motor velocity constant (RPM/V) */
    bool foc_closed_loop;           /* True if active closed-loop FOC current regulation is engaged */
} MotorControl_t;

/* Core Motor Control API */
void Motor_Init(void);
void Motor_ApplyProfile(const MotorProfile_t* prof);
void Motor_StartCharacterization(void);
void Motor_CalibrateCurrentOffsets(void);
void Motor_Start(void);
void Motor_Stop(void);
void Motor_Brake(void);
void Motor_SetDuty(float duty_pct);
void Motor_IncDuty(void);
void Motor_DecDuty(void);
void Motor_SetTorqueLimit(float amps);
void Motor_IncTorqueLimit(void);
void Motor_DecTorqueLimit(void);
void Motor_SetSpeed(float rpm);
void Motor_SetDirection(int8_t dir);
void Motor_SetMode(MotorMode_t mode);
void Motor_ToggleClosedLoopFOC(void);
bool Motor_IsClosedLoopFOC(void);
void Motor_TogglePotControl(void);
void Motor_SetPotControl(bool enable);
void Motor_ResetFault(void);
void Motor_ResetOpticalCount(void);
void Motor_SetSinusoidalOutput(float freq_hz, float duty_pct);
void Motor_Update1ms(void);
void Motor_PWM_UpdateISR(void);
void Motor_OpticalFallingEdgeISR(uint32_t capture_val);
void Motor_OpticalResetEdgeState(void);
void Motor_GetTelemetry(MotorTelemetry_t* telem);
const MotorControl_t* Motor_GetControl(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_CONTROL_H */
