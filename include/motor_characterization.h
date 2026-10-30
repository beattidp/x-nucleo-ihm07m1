#ifndef __MOTOR_CHARACTERIZATION_H
#define __MOTOR_CHARACTERIZATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>

typedef enum {
    CHAR_STATE_IDLE = 0,
    CHAR_STATE_CALIBRATING_OFFSETS, /* Step 1: Zero-current ADC calibration & bus check */
    CHAR_STATE_MEASURING_R_UV,      /* Step 2a: Resistance Phase U -> V */
    CHAR_STATE_MEASURING_R_VW,      /* Step 2b: Resistance Phase V -> W */
    CHAR_STATE_MEASURING_R_WU,      /* Step 2c: Resistance Phase W -> U */
    CHAR_STATE_ALIGNMENT_STICTION,  /* Step 3: DC alignment and minimum stiction current */
    CHAR_STATE_DYNAMIC_SPIN_50HZ,   /* Step 4a: Dynamic spin at 50 Hz electrical */
    CHAR_STATE_DYNAMIC_SPIN_100HZ,  /* Step 4b: Dynamic spin at 100 Hz electrical */
    CHAR_STATE_CALCULATING,         /* Step 5: Process results */
    CHAR_STATE_COMPLETE,            /* Finished: Display summary and wait for user confirm */
    CHAR_STATE_ABORTED              /* Aborted by user or fault */
} CharState_t;

typedef struct {
    /* Winding Resistance Results */
    float r_uv_ohm;
    float r_vw_ohm;
    float r_wu_ohm;
    float r_phase_ohm;          /* Rs = R_LL / 2 */
    float phase_unbalance_pct;  /* (R_max - R_min) / R_avg * 100% */
    
    /* Stiction / Holding */
    float stiction_current_amps;
    float min_align_duty_pct;

    /* Dynamic Back-EMF & KV Results */
    float kv_rating;            /* Estimated RPM / Volt */
    float ke_v_krpm;            /* Back-EMF constant V / kRPM */
    float recommended_base_duty;
    float recommended_vf_slope;
    float eddy_damping_ma_rpm;  /* Characterized eddy current brake drag slope (mA / RPM) */
    float recommended_load_comp;/* Recommended IR load compensation gain (V / A) */

    /* Status flags */
    bool valid;
    char status_msg[48];
} CharResults_t;

void MotorChar_Start(void);
void MotorChar_Abort(const char* reason);
void MotorChar_Update1ms(void);
bool MotorChar_IsRunning(void);
CharState_t MotorChar_GetState(void);
const CharResults_t* MotorChar_GetResults(void);
uint8_t MotorChar_GetProgressPct(void);
void MotorChar_ApplyResults(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_CHARACTERIZATION_H */
