#ifndef __FOC_H
#define __FOC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FOC_ONE_BY_SQRT3    0.57735026919f
#define FOC_SQRT3_BY_TWO    0.86602540378f
#define FOC_PI              3.141592653589793f
#define FOC_TWO_PI          6.283185307179586f

/**
 * @brief 3-Phase, Stationary Orthogonal (Alpha/Beta), and Rotating (D/Q) Currents
 */
typedef struct {
    float ia;           /* Phase A current in Amperes */
    float ib;           /* Phase B current in Amperes */
    float ic;           /* Phase C current in Amperes */
    float i_alpha;      /* Stationary orthogonal frame alpha current */
    float i_beta;       /* Stationary orthogonal frame beta current */
    float id;           /* Direct (flux-producing) current in Amperes */
    float iq;           /* Quadrature (torque-producing) current in Amperes */
    float id_filt;      /* Filtered Id current */
    float iq_filt;      /* Filtered Iq current */
} FocCurrents_t;

/**
 * @brief Rotating (D/Q), Stationary Orthogonal (Alpha/Beta), and Phase PWM Voltages
 */
typedef struct {
    float vd;           /* Direct axis voltage command (V) */
    float vq;           /* Quadrature axis voltage command (V) */
    float v_alpha;      /* Alpha axis voltage (V) */
    float v_beta;       /* Beta axis voltage (V) */
    uint16_t duty_u;    /* Timer CCR1 compare count */
    uint16_t duty_v;    /* Timer CCR2 compare count */
    uint16_t duty_w;    /* Timer CCR3 compare count */
} FocVoltages_t;

/**
 * @brief High-Level Field-Oriented Control State
 */
typedef struct {
    float theta_e;           /* Electrical rotor angle in radians [-PI, PI) */
    float omega_e;           /* Electrical rotor speed in rad/s */
    float id_ref;            /* Flux current setpoint (Amperes, default 0.0 A) */
    float iq_ref;            /* Torque current setpoint (Amperes) */
    FocCurrents_t currents;  /* Real-time transformed currents */
    FocVoltages_t voltages;  /* Real-time transformed voltages */
    bool cordic_ready;       /* True if STM32G4 CORDIC accelerator is operational */
    uint32_t cycle_count;    /* Total FOC transformation executions */
} FocState_t;

extern FocState_t g_foc;

/* Core FOC Initialization & Lifecycle */
void FOC_Init(void);
void FOC_Reset(void);

/* STM32G4 Hardware CORDIC Fast Math */
void FOC_CordicSinCos(float theta_rad, float* sin_out, float* cos_out);

/* Vector Transformation Functions */
void FOC_Clarke(float ia, float ib, float ic, float* i_alpha, float* i_beta);
void FOC_Park(float i_alpha, float i_beta, float theta_rad, float* id, float* iq);
void FOC_InvPark(float vd, float vq, float theta_rad, float* v_alpha, float* v_beta);
void FOC_SVM(float v_alpha, float v_beta, float vbus, uint16_t arr, uint16_t* u, uint16_t* v, uint16_t* w);

/* FOC Pipeline Execution */
void FOC_ProcessCurrents(float ia, float ib, float ic, float theta_rad);

#ifdef __cplusplus
}
#endif

#endif /* __FOC_H */
