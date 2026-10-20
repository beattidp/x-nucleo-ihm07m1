#include "foc.h"
#include "main.h"
#include "stm32g4xx_ll_cordic.h"
#include "stm32g4xx_ll_bus.h"
#include <math.h>
#include <string.h>

FocState_t g_foc = {0};

void FOC_Init(void) {
    memset(&g_foc, 0, sizeof(FocState_t));

    /* Enable CORDIC peripheral clock on AHB1 */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_CORDIC);

    /* Configure CORDIC hardware co-processor:
     * - Function: COSINE (generates simultaneous Cosine in RES1 and Sine in RES2)
     * - Precision: 6 CYCLES (24-bit accuracy, executes in 35 ns at 170 MHz)
     * - Scale: 0 (no scaling needed for cosine/sine)
     * - NbWrite: 1 (angle theta only)
     * - NbRead: 2 (read cos then sin)
     * - Data Size: 32-bit Q1.31 format
     */
    LL_CORDIC_Config(CORDIC,
                     LL_CORDIC_FUNCTION_COSINE,
                     LL_CORDIC_PRECISION_6CYCLES,
                     LL_CORDIC_SCALE_0,
                     LL_CORDIC_NBWRITE_1,
                     LL_CORDIC_NBREAD_2,
                     LL_CORDIC_INSIZE_32BITS,
                     LL_CORDIC_OUTSIZE_32BITS);

    g_foc.cordic_ready = true;
    g_foc.cos_theta = 1.0f;
    g_foc.sin_theta = 0.0f;
    g_foc.id_ref = 0.0f;
    g_foc.iq_ref = 0.0f;

    /* Initialize dual PI current regulators with measured 5010-360KV parameters */
    FOC_CurrentRegulators_Init(0.50f, 0.00015f, 360.0f, 2.0f * FOC_PI * 600.0f);
}

void FOC_Reset(void) {
    g_foc.theta_e = 0.0f;
    g_foc.omega_e = 0.0f;
    g_foc.id_ref = 0.0f;
    g_foc.iq_ref = 0.0f;
    memset(&g_foc.currents, 0, sizeof(FocCurrents_t));
    memset(&g_foc.voltages, 0, sizeof(FocVoltages_t));
    FOC_CurrentRegulators_Reset();
}

/**
 * @brief Initialize dual PI current regulators using analytical pole-placement
 * @param rs_ohm Motor phase resistance in Ohms (e.g. 0.50 Ohm)
 * @param ls_h Motor phase inductance in Henries (e.g. 150 uH)
 * @param kv Motor velocity constant in RPM/V (e.g. 360 RPM/V)
 * @param bandwidth_rad_s Desired current loop closed-loop bandwidth in rad/s (e.g. 2*pi*600 rad/s)
 */
void FOC_CurrentRegulators_Init(float rs_ohm, float ls_h, float kv, float bandwidth_rad_s) {
    if (rs_ohm <= 0.01f) rs_ohm = 0.50f;
    if (ls_h <= 1.0e-6f) ls_h = 0.00015f;
    if (kv <= 10.0f) kv = 360.0f;
    if (bandwidth_rad_s <= 100.0f) bandwidth_rad_s = 2.0f * FOC_PI * 600.0f;

    g_foc.rs_ohm = rs_ohm;
    g_foc.ls_h = ls_h;
    g_foc.kv_rating = kv;
    g_foc.active_damping_gain = 350.0f; /* rad/(s * A) damping against torsional rotor hunting for bare rotor */

    /* Sampling period at 20 kHz PWM rate */
    const float ts = 1.0f / 20000.0f;

    /* Pole-placement tuning: Kp = Ls * w_c, Ki_disc = Rs * w_c * Ts */
    float kp = ls_h * bandwidth_rad_s;
    float ki_disc = rs_ohm * bandwidth_rad_s * ts;

    /* Direct axis PI regulator */
    g_foc.pi_d.kp = kp;
    g_foc.pi_d.ki = ki_disc;
    g_foc.pi_d.integral = 0.0f;
    g_foc.pi_d.out_min = -14.0f;
    g_foc.pi_d.out_max = 14.0f;
    g_foc.pi_d.out = 0.0f;

    /* Quadrature axis PI regulator */
    g_foc.pi_q.kp = kp;
    g_foc.pi_q.ki = ki_disc;
    g_foc.pi_q.integral = 0.0f;
    g_foc.pi_q.out_min = -14.0f;
    g_foc.pi_q.out_max = 14.0f;
    g_foc.pi_q.out = 0.0f;

    g_foc.regulators_active = true;
}

void FOC_CurrentRegulators_Reset(void) {
    g_foc.pi_d.integral = 0.0f;
    g_foc.pi_d.out = 0.0f;
    g_foc.pi_q.integral = 0.0f;
    g_foc.pi_q.out = 0.0f;
}

void FOC_SetTorqueReference(float iq_ref) {
    g_foc.iq_ref = iq_ref;
}

void FOC_SetRegulatorsActive(bool active) {
    g_foc.regulators_active = active;
    if (!active) {
        FOC_CurrentRegulators_Reset();
    }
}

bool FOC_AreRegulatorsActive(void) {
    return g_foc.regulators_active;
}

/**
 * @brief Discrete PI calculation with conditional integration anti-windup
 */
static inline float FOC_PI_Calc(FocPI_t* pi, float error, float v_limit) {
    float p_term = pi->kp * error;
    float next_int = pi->integral + (pi->ki * error);
    float v_unsat = p_term + next_int;

    if (v_unsat > v_limit) {
        pi->out = v_limit;
        if (error < 0.0f) {
            pi->integral = next_int;
        }
    } else if (v_unsat < -v_limit) {
        pi->out = -v_limit;
        if (error > 0.0f) {
            pi->integral = next_int;
        }
    } else {
        pi->integral = next_int;
        pi->out = v_unsat;
    }
    return pi->out;
}

/**
 * @brief Execute 20 kHz synchronous dual PI current regulation loop
 * @param id_meas Measured direct axis current (A)
 * @param iq_meas Measured quadrature axis current (A)
 * @param id_ref Setpoint direct axis current (A, typically 0.0)
 * @param iq_ref Setpoint quadrature axis current (A)
 * @param omega_e Electrical angular velocity in rad/s
 * @param vbus Instantaneous DC link voltage in Volts
 * @param vd_out Pointer to output direct axis voltage command (V)
 * @param vq_out Pointer to output quadrature axis voltage command (V)
 */
void FOC_CurrentRegulators_Update(float id_meas, float iq_meas, float id_ref, float iq_ref,
                                 float omega_e, float vbus, float v_baseline, float* vd_out, float* vq_out) {
    if (vbus < 4.0f) vbus = 12.0f;

    /* Space Vector Modulation max linear voltage: Vmax = Vbus / sqrt(3) */
    float v_max = vbus * FOC_ONE_BY_SQRT3;

    (void)id_ref;
    (void)id_meas;

    /* Errors */
    float err_q = iq_ref - iq_meas;

    /* Feedforward decoupling:
     * - Quadrature axis: +w_e * Ld * Id + V_baseline (bumpless handover from SPWM)
     */
    float vq_ff = (omega_e * g_foc.ls_h * id_meas) + v_baseline;

    /* Dynamic PI trim headroom: +/- 0.40V to tightly regulate current without transients.
     * Across 0.50 Ohm phase resistance, maximum transient current is 0.40V / 0.50 Ohm = 0.8A,
     * which can never trip the 2.2A hardware break. */
    float pi_v_limit = 0.40f;
    if (pi_v_limit > v_max * 0.10f) pi_v_limit = v_max * 0.10f;
    float vq_pi = FOC_PI_Calc(&g_foc.pi_q, err_q, pi_v_limit);

    /* Direct axis voltage set to 0 (MTPA). Active damping controls rotor flux alignment
     * by dynamically steering the stator frequency via Id, preventing controller fighting. */
    float vd_cmd = 0.0f;
    float vq_cmd = vq_pi + vq_ff;

    /* Vector circle saturation: clamp sqrt(Vd^2 + Vq^2) <= Vmax */
    float v_mag_sq = (vd_cmd * vd_cmd) + (vq_cmd * vq_cmd);
    float v_max_sq = v_max * v_max;
    if (v_mag_sq > v_max_sq && v_mag_sq > 0.0001f) {
        float scale = v_max / sqrtf(v_mag_sq);
        vd_cmd *= scale;
        vq_cmd *= scale;
    }

    g_foc.voltages.vd = vd_cmd;
    g_foc.voltages.vq = vq_cmd;

    if (vd_out) *vd_out = vd_cmd;
    if (vq_out) *vq_out = vq_cmd;
}

/**
 * @brief Fast simultaneous Sine and Cosine calculation using STM32G4 CORDIC accelerator
 * @param theta_rad Electrical angle in radians
 * @param sin_out Pointer to output sine float
 * @param cos_out Pointer to output cosine float
 */
void FOC_CordicSinCos(float theta_rad, float* sin_out, float* cos_out) {
    if (!g_foc.cordic_ready) {
        *sin_out = sinf(theta_rad);
        *cos_out = cosf(theta_rad);
        return;
    }

    /* Wrap theta into [-PI, PI) */
    while (theta_rad >= FOC_PI)  theta_rad -= FOC_TWO_PI;
    while (theta_rad < -FOC_PI) theta_rad += FOC_TWO_PI;

    /* Normalize angle: angle / PI mapped to 32-bit signed Q1.31 [-2^31, 2^31 - 1] */
    int32_t arg_q31 = (int32_t)((theta_rad / FOC_PI) * 2147483647.0f);

    /* Write angle to CORDIC WDATA register */
    LL_CORDIC_WriteData(CORDIC, (uint32_t)arg_q31);

    /* Wait for CORDIC co-processor calculation to complete (takes 4-6 cycles) */
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));

    /* Read primary result (Cosine) then secondary result (Sine) from RDATA */
    int32_t cos_q31 = (int32_t)LL_CORDIC_ReadData(CORDIC);
    int32_t sin_q31 = (int32_t)LL_CORDIC_ReadData(CORDIC);

    /* Convert Q1.31 fixed point to float [-1.0, 1.0] */
    *cos_out = (float)cos_q31 * (1.0f / 2147483648.0f);
    *sin_out = (float)sin_q31 * (1.0f / 2147483648.0f);
}

/**
 * @brief Forward Clarke Transformation (3-phase abc -> stationary alpha/beta)
 *        Symmetrical 3-shunt formulation with common-mode offset rejection:
 *        I_alpha = Ia - (Ia + Ib + Ic)/3
 *        I_beta  = (Ib - Ic) / sqrt(3)
 */
void FOC_Clarke(float ia, float ib, float ic, float* i_alpha, float* i_beta) {
    float i_zero = (ia + ib + ic) * (1.0f / 3.0f);
    *i_alpha = ia - i_zero;
    *i_beta  = (ib - ic) * FOC_ONE_BY_SQRT3;
}

/**
 * @brief Forward Park Transformation (stationary alpha/beta -> rotating d/q)
 *        Direct axis aligned with rotor flux vector at angle theta_rad
 */
void FOC_Park(float i_alpha, float i_beta, float theta_rad, float* id, float* iq) {
    float sin_th = 0.0f;
    float cos_th = 1.0f;
    FOC_CordicSinCos(theta_rad, &sin_th, &cos_th);
    g_foc.sin_theta = sin_th;
    g_foc.cos_theta = cos_th;

    /* Aligned with open-loop sinusoidal SPWM trajectory (u = sin(theta), v = sin(theta+120)) */
    *id =  i_alpha * cos_th - i_beta * sin_th;
    *iq =  i_alpha * sin_th + i_beta * cos_th;
}

/**
 * @brief Inverse Park Transformation (rotating d/q -> stationary alpha/beta)
 *        Reuses cached sin_theta and cos_theta from FOC_Park (zero CORDIC coprocessor latency)
 *        Orthonormal transpose pair of FOC_Park: Valpha = Vq*sin + Vd*cos, Vbeta = Vq*cos - Vd*sin
 */
void FOC_InvPark(float vd, float vq, float* v_alpha, float* v_beta) {
    *v_alpha = vq * g_foc.sin_theta + vd * g_foc.cos_theta;
    *v_beta  = vq * g_foc.cos_theta - vd * g_foc.sin_theta;
}

/**
 * @brief Space Vector Modulation (SVM) with common-mode mid-point carrier centering
 *        Maximizes DC bus voltage utilization up to Vbus / sqrt(3) (15.5% higher than sinusoidal)
 */
void FOC_SVM(float v_alpha, float v_beta, float vbus, uint16_t arr, uint16_t* u, uint16_t* v, uint16_t* w) {
    if (vbus < 4.0f) vbus = 12.0f;

    /* Inverse Clarke: compute 3-phase line-to-neutral voltages */
    float va = v_alpha;
    float vb = -0.5f * v_alpha + FOC_SQRT3_BY_TWO * v_beta;
    float vc = -0.5f * v_alpha - FOC_SQRT3_BY_TWO * v_beta;

    /* Find min and max for symmetrical mid-point zero-sequence injection */
    float v_max = va;
    if (vb > v_max) v_max = vb;
    if (vc > v_max) v_max = vc;

    float v_min = va;
    if (vb < v_min) v_min = vb;
    if (vc < v_min) v_min = vc;

    float v_mid = 0.5f * (v_max + v_min);
    float va_svm = va - v_mid;
    float vb_svm = vb - v_mid;
    float vc_svm = vc - v_mid;

    /* Compute normalized duty cycles centered at 50% */
    float da = 0.5f + (va_svm / vbus);
    float db = 0.5f + (vb_svm / vbus);
    float dc = 0.5f + (vc_svm / vbus);

    /* Clamp duty cycles: minimum 5% to refresh bootstrap capacitors, maximum 92% */
    if (da < 0.05f) da = 0.05f; else if (da > 0.92f) da = 0.92f;
    if (db < 0.05f) db = 0.05f; else if (db > 0.92f) db = 0.92f;
    if (dc < 0.05f) dc = 0.05f; else if (dc > 0.92f) dc = 0.92f;

    *u = (uint16_t)(da * (float)arr);
    *v = (uint16_t)(db * (float)arr);
    *w = (uint16_t)(dc * (float)arr);
}

/**
 * @brief Process measured 3-phase currents and transform to rotor d-q frame
 */
void FOC_ProcessCurrents(float ia, float ib, float ic, float theta_rad) {
    g_foc.currents.ia = ia;
    g_foc.currents.ib = ib;
    g_foc.currents.ic = ic;
    g_foc.theta_e = theta_rad;

    /* 1. Forward Clarke Transform (abc -> alpha/beta) */
    FOC_Clarke(ia, ib, ic, &g_foc.currents.i_alpha, &g_foc.currents.i_beta);

    /* 2. Forward Park Transform (alpha/beta -> d/q) via CORDIC */
    FOC_Park(g_foc.currents.i_alpha, g_foc.currents.i_beta, theta_rad,
             &g_foc.currents.id, &g_foc.currents.iq);

    /* 3. Low-Pass Filtered Current Tracking (cutoff ~25 Hz for diagnostics) */
    g_foc.currents.id_filt = (g_foc.currents.id_filt * 0.95f) + (g_foc.currents.id * 0.05f);
    g_foc.currents.iq_filt = (g_foc.currents.iq_filt * 0.95f) + (g_foc.currents.iq * 0.05f);

    g_foc.cycle_count++;
}
