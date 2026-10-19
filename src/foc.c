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
     * - Precision: 4 CYCLES (~16-bit accuracy, executes in 23.5 ns at 170 MHz)
     * - Scale: 0 (no scaling needed for cosine/sine)
     * - NbWrite: 1 (angle theta only)
     * - NbRead: 2 (read cos then sin)
     * - Data Size: 32-bit Q1.31 format
     */
    LL_CORDIC_Config(CORDIC,
                     LL_CORDIC_FUNCTION_COSINE,
                     LL_CORDIC_PRECISION_4CYCLES,
                     LL_CORDIC_SCALE_0,
                     LL_CORDIC_NBWRITE_1,
                     LL_CORDIC_NBREAD_2,
                     LL_CORDIC_INSIZE_32BITS,
                     LL_CORDIC_OUTSIZE_32BITS);

    g_foc.cordic_ready = true;
    g_foc.id_ref = 0.0f;
    g_foc.iq_ref = 0.0f;
}

void FOC_Reset(void) {
    g_foc.theta_e = 0.0f;
    g_foc.omega_e = 0.0f;
    g_foc.id_ref = 0.0f;
    g_foc.iq_ref = 0.0f;
    memset(&g_foc.currents, 0, sizeof(FocCurrents_t));
    memset(&g_foc.voltages, 0, sizeof(FocVoltages_t));
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

    /* Read primary result (Cosine) then secondary result (Sine) from RDATA */
    int32_t cos_q31 = (int32_t)LL_CORDIC_ReadData(CORDIC);
    int32_t sin_q31 = (int32_t)LL_CORDIC_ReadData(CORDIC);

    /* Convert Q1.31 fixed point to float [-1.0, 1.0] */
    *cos_out = (float)cos_q31 * (1.0f / 2147483648.0f);
    *sin_out = (float)sin_q31 * (1.0f / 2147483648.0f);
}

/**
 * @brief Forward Clarke Transformation (3-phase abc -> stationary alpha/beta)
 *        Assumes balanced 3-phase system with isolated neutral (Ia + Ib + Ic = 0)
 */
void FOC_Clarke(float ia, float ib, float ic, float* i_alpha, float* i_beta) {
    (void)ic;
    *i_alpha = ia;
    *i_beta  = (ia + 2.0f * ib) * FOC_ONE_BY_SQRT3;
}

/**
 * @brief Forward Park Transformation (stationary alpha/beta -> rotating d/q)
 *        Direct axis aligned with rotor flux vector at angle theta_rad
 */
void FOC_Park(float i_alpha, float i_beta, float theta_rad, float* id, float* iq) {
    float sin_th = 0.0f;
    float cos_th = 1.0f;
    FOC_CordicSinCos(theta_rad, &sin_th, &cos_th);

    *id =  i_alpha * cos_th + i_beta * sin_th;
    *iq = -i_alpha * sin_th + i_beta * cos_th;
}

/**
 * @brief Inverse Park Transformation (rotating d/q -> stationary alpha/beta)
 */
void FOC_InvPark(float vd, float vq, float theta_rad, float* v_alpha, float* v_beta) {
    float sin_th = 0.0f;
    float cos_th = 1.0f;
    FOC_CordicSinCos(theta_rad, &sin_th, &cos_th);

    *v_alpha = vd * cos_th - vq * sin_th;
    *v_beta  = vd * sin_th + vq * cos_th;
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
