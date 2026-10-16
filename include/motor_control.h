#ifndef __MOTOR_CONTROL_H
#define __MOTOR_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

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
    MOTOR_STATE_FAULT
} MotorState_t;

typedef struct MotorTelemetry_s {
    float vbus_v;                   /* DC Bus voltage in Volts */
    float temp_c;                   /* Driver board temperature in deg C */
    float pot_pct;                  /* User speed potentiometer (0 - 100 %) */
    float current_a_amps;           /* Phase A current in Amps */
    float current_b_amps;           /* Phase B current in Amps */
    float current_c_amps;           /* Phase C current in Amps */
    float current_peak_amps;        /* Highest instantaneous phase current */
    uint8_t hall_state;             /* Current Hall state (1 to 6) */
    bool fault_active;              /* L6230 DIAG line active (low) */
    uint32_t sample_count;          /* Telemetry sample count */
} MotorTelemetry_t;

typedef struct MotorControl_s {
    MotorMode_t mode;
    MotorState_t state;
    int8_t direction;               /* +1 for CW, -1 for CCW */
    float target_speed_rpm;         /* Target speed in RPM */
    float actual_speed_rpm;         /* Measured or estimated RPM */
    float duty_pct;                 /* Commanded duty cycle (0.0 to 100.0%) */
    float electrical_freq_hz;       /* Commutation electrical frequency in Hz */
    uint8_t pole_pairs;             /* Motor pole pairs (default: 7) */
    float current_limit_amps;       /* Hardware trip threshold (default: 1.2 A) */
    uint32_t run_time_ms;           /* Elapsed motor run time in ms */
} MotorControl_t;

/* Core Motor Control API */
void Motor_Init(void);
void Motor_Start(void);
void Motor_Stop(void);
void Motor_Brake(void);
void Motor_SetDuty(float duty_pct);
void Motor_SetSpeed(float rpm);
void Motor_SetDirection(int8_t dir);
void Motor_SetMode(MotorMode_t mode);
void Motor_ResetFault(void);
void Motor_Update1ms(void);
void Motor_GetTelemetry(MotorTelemetry_t* telem);
const MotorControl_t* Motor_GetControl(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_CONTROL_H */
