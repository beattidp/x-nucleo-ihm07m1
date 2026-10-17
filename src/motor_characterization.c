#include "motor_characterization.h"
#include "motor_control.h"
#include "motor_profiles.h"
#include "ihm07m1.h"
#include <string.h>
#include <math.h>

#define PI_FLOAT                    3.14159265358979323846f
#define TWO_PI                      (2.0f * PI_FLOAT)

static CharState_t s_char_state = CHAR_STATE_IDLE;
static CharResults_t s_results = {0};
static uint32_t s_step_timer_ms = 0;
static uint32_t s_total_timer_ms = 0;

/* Accumulated measurements for averaging */
static float s_accum_vbus = 0.0f;
static float s_accum_curr = 0.0f;
static uint16_t s_sample_count = 0;

/* Dynamic measurement storage */
static float s_v50 = 0.0f;
static float s_i50 = 0.0f;
static float s_rpm50 = 0.0f;
static float s_v100 = 0.0f;
static float s_i100 = 0.0f;
static float s_rpm100 = 0.0f;

void MotorChar_Start(void) {
    memset(&s_results, 0, sizeof(s_results));
    s_results.valid = false;
    strncpy(s_results.status_msg, "Initializing Characterization...", sizeof(s_results.status_msg) - 1);

    s_char_state = CHAR_STATE_CALIBRATING_OFFSETS;
    s_step_timer_ms = 0;
    s_total_timer_ms = 0;
    s_accum_vbus = 0.0f;
    s_accum_curr = 0.0f;
    s_sample_count = 0;
}

void MotorChar_Abort(const char* reason) {
    Motor_SetSinusoidalOutput(0.0f, 0.0f);
    IHM07M1_DisableBridge();
    IHM07M1_StopPWM();
    s_char_state = CHAR_STATE_ABORTED;
    if (reason) {
        strncpy(s_results.status_msg, reason, sizeof(s_results.status_msg) - 1);
    } else {
        strncpy(s_results.status_msg, "Aborted by user.", sizeof(s_results.status_msg) - 1);
    }
}

bool MotorChar_IsRunning(void) {
    return (s_char_state != CHAR_STATE_IDLE &&
            s_char_state != CHAR_STATE_COMPLETE &&
            s_char_state != CHAR_STATE_ABORTED);
}

CharState_t MotorChar_GetState(void) {
    return s_char_state;
}

const CharResults_t* MotorChar_GetResults(void) {
    return &s_results;
}

uint8_t MotorChar_GetProgressPct(void) {
    /* Total duration approx 7000 ms */
    if (s_char_state == CHAR_STATE_IDLE) return 0;
    if (s_char_state == CHAR_STATE_COMPLETE) return 100;
    if (s_char_state == CHAR_STATE_ABORTED) return 0;

    uint32_t pct = (s_total_timer_ms * 100) / 7200;
    if (pct > 99) pct = 99;
    return (uint8_t)pct;
}

void MotorChar_ApplyResults(void) {
    if (!s_results.valid) return;
    MotorProfiles_SaveCharacterized(
        s_results.r_phase_ohm,
        s_results.kv_rating,
        s_results.recommended_base_duty,
        s_results.recommended_vf_slope
    );
    strncpy(s_results.status_msg, "Profile Saved & Activated!", sizeof(s_results.status_msg) - 1);
}

/**
 * @brief Helper to measure DC line-to-line pulse
 */
static void ApplyPulse(uint8_t phase_mask, uint16_t duty_u, uint16_t duty_v, uint16_t duty_w) {
    IHM07M1_EnablePhase(phase_mask);
    IHM07M1_SetPWM(duty_u, duty_v, duty_w);
}

/**
 * @brief Periodic 1 ms Characterization Engine
 */
void MotorChar_Update1ms(void) {
    if (!MotorChar_IsRunning()) {
        return;
    }

    s_step_timer_ms++;
    s_total_timer_ms++;

    /* Fault Check */
    if (IHM07M1_IsFaultActive()) {
        MotorChar_Abort("Hardware Break / Overcurrent Tripped!");
        return;
    }

    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);

    switch (s_char_state) {
        /* ====================================================================
         * Step 1: Calibrate Shunt Offsets & Verify Bus Voltage (300 ms)
         * ==================================================================== */
        case CHAR_STATE_CALIBRATING_OFFSETS:
            IHM07M1_DisableBridge();
            IHM07M1_StopPWM();
            strncpy(s_results.status_msg, "Step 1/5: Calibrating Shunt Offsets...", sizeof(s_results.status_msg) - 1);

            if (s_step_timer_ms == 50) {
                if (telem.vbus_v < 8.0f) {
                    MotorChar_Abort("VBUS < 8V (Check Power Supply)");
                    return;
                }
                Motor_CalibrateCurrentOffsets();
            }

            if (s_step_timer_ms >= 300) {
                s_step_timer_ms = 0;
                s_accum_curr = 0.0f;
                s_accum_vbus = 0.0f;
                s_sample_count = 0;
                IHM07M1_StartPWM();
                s_char_state = CHAR_STATE_MEASURING_R_UV;
            }
            break;

        /* ====================================================================
         * Step 2a: Resistance Pulse Phase U -> V (250 ms)
         * ==================================================================== */
        case CHAR_STATE_MEASURING_R_UV:
            strncpy(s_results.status_msg, "Step 2/5: Measuring Winding Resistance...", sizeof(s_results.status_msg) - 1);
            {
                /* 5% duty DC pulse: safe, low power across all motor types */
                uint16_t test_duty = (uint16_t)(0.05f * (float)IHM07M1_PWM_ARR);
                if (s_step_timer_ms < 50) {
                    ApplyPulse(0x00, 0, 0, 0); /* 50 ms settling */
                } else if (s_step_timer_ms < 220) {
                    /* U=HIGH (+), V=LOW (-), W=Hi-Z */
                    ApplyPulse(0x03, test_duty, 0, 0);
                    if (s_step_timer_ms > 100) {
                        s_accum_curr += telem.current_peak_amps;
                        s_accum_vbus += telem.vbus_v;
                        s_sample_count++;
                    }
                } else {
                    ApplyPulse(0x00, 0, 0, 0);
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.01f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 12.0f;
                    float v_applied = 0.05f * avg_vbus;

                    if (avg_i < 0.05f) avg_i = 0.05f;
                    s_results.r_uv_ohm = v_applied / avg_i;

                    s_step_timer_ms = 0;
                    s_accum_curr = 0.0f;
                    s_accum_vbus = 0.0f;
                    s_sample_count = 0;
                    s_char_state = CHAR_STATE_MEASURING_R_VW;
                }
            }
            break;

        /* ====================================================================
         * Step 2b: Resistance Pulse Phase V -> W (250 ms)
         * ==================================================================== */
        case CHAR_STATE_MEASURING_R_VW:
            {
                uint16_t test_duty = (uint16_t)(0.05f * (float)IHM07M1_PWM_ARR);
                if (s_step_timer_ms < 50) {
                    ApplyPulse(0x00, 0, 0, 0);
                } else if (s_step_timer_ms < 220) {
                    /* V=HIGH (+), W=LOW (-), U=Hi-Z */
                    ApplyPulse(0x06, 0, test_duty, 0);
                    if (s_step_timer_ms > 100) {
                        s_accum_curr += telem.current_peak_amps;
                        s_accum_vbus += telem.vbus_v;
                        s_sample_count++;
                    }
                } else {
                    ApplyPulse(0x00, 0, 0, 0);
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.01f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 12.0f;
                    float v_applied = 0.05f * avg_vbus;

                    if (avg_i < 0.05f) avg_i = 0.05f;
                    s_results.r_vw_ohm = v_applied / avg_i;

                    s_step_timer_ms = 0;
                    s_accum_curr = 0.0f;
                    s_accum_vbus = 0.0f;
                    s_sample_count = 0;
                    s_char_state = CHAR_STATE_MEASURING_R_WU;
                }
            }
            break;

        /* ====================================================================
         * Step 2c: Resistance Pulse Phase W -> U (250 ms)
         * ==================================================================== */
        case CHAR_STATE_MEASURING_R_WU:
            {
                uint16_t test_duty = (uint16_t)(0.05f * (float)IHM07M1_PWM_ARR);
                if (s_step_timer_ms < 50) {
                    ApplyPulse(0x00, 0, 0, 0);
                } else if (s_step_timer_ms < 220) {
                    /* W=HIGH (+), U=LOW (-), V=Hi-Z */
                    ApplyPulse(0x05, 0, 0, test_duty);
                    if (s_step_timer_ms > 100) {
                        s_accum_curr += telem.current_peak_amps;
                        s_accum_vbus += telem.vbus_v;
                        s_sample_count++;
                    }
                } else {
                    ApplyPulse(0x00, 0, 0, 0);
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.01f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 12.0f;
                    float v_applied = 0.05f * avg_vbus;

                    if (avg_i < 0.05f) avg_i = 0.05f;
                    s_results.r_wu_ohm = v_applied / avg_i;

                    /* Compute Line-to-Line and Phase Resistance */
                    float r_ll = (s_results.r_uv_ohm + s_results.r_vw_ohm + s_results.r_wu_ohm) / 3.0f;
                    s_results.r_phase_ohm = r_ll / 2.0f;

                    /* Phase Unbalance % */
                    float r_max = s_results.r_uv_ohm;
                    if (s_results.r_vw_ohm > r_max) r_max = s_results.r_vw_ohm;
                    if (s_results.r_wu_ohm > r_max) r_max = s_results.r_wu_ohm;
                    float r_min = s_results.r_uv_ohm;
                    if (s_results.r_vw_ohm < r_min) r_min = s_results.r_vw_ohm;
                    if (s_results.r_wu_ohm < r_min) r_min = s_results.r_wu_ohm;

                    s_results.phase_unbalance_pct = ((r_max - r_min) / r_ll) * 100.0f;

                    s_step_timer_ms = 0;
                    s_accum_curr = 0.0f;
                    s_sample_count = 0;
                    s_char_state = CHAR_STATE_ALIGNMENT_STICTION;
                }
            }
            break;

        /* ====================================================================
         * Step 3: Rotor Alignment & Stiction Threshold Test (1000 ms)
         * ==================================================================== */
        case CHAR_STATE_ALIGNMENT_STICTION:
            strncpy(s_results.status_msg, "Step 3/5: Rotor Alignment & Stiction Test...", sizeof(s_results.status_msg) - 1);
            {
                /* Ramp alignment duty from 12% to 22% at angle 0 */
                float progress = (float)s_step_timer_ms / 1000.0f;
                float align_duty = 12.0f + progress * 10.0f;

                Motor_SetSinusoidalOutput(0.0f, align_duty);
                IHM07M1_EnablePhase(0x07);

                if (s_step_timer_ms > 600) {
                    s_accum_curr += telem.current_peak_amps;
                    s_sample_count++;
                }

                if (s_step_timer_ms >= 1000) {
                    s_results.stiction_current_amps = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.35f;
                    s_results.min_align_duty_pct = 20.0f;

                    s_step_timer_ms = 0;
                    s_accum_curr = 0.0f;
                    s_accum_vbus = 0.0f;
                    s_sample_count = 0;
                    s_char_state = CHAR_STATE_DYNAMIC_SPIN_50HZ;
                }
            }
            break;

        /* ====================================================================
         * Step 4a: Dynamic Spin at 50 Hz Electrical (2500 ms)
         * ==================================================================== */
        case CHAR_STATE_DYNAMIC_SPIN_50HZ:
            strncpy(s_results.status_msg, "Step 4/5: Dynamic Back-EMF Spin (50 Hz)...", sizeof(s_results.status_msg) - 1);
            {
                /* 500 ms ramp 0->50Hz, then 2000 ms steady */
                float freq = 50.0f;
                if (s_step_timer_ms < 500) {
                    freq = 2.0f + ((float)s_step_timer_ms / 500.0f) * 48.0f;
                }

                float duty_50 = 22.0f;
                Motor_SetSinusoidalOutput(freq, duty_50);
                IHM07M1_EnablePhase(0x07);

                if (s_step_timer_ms > 1200) {
                    s_accum_curr += telem.current_peak_amps;
                    s_accum_vbus += telem.vbus_v;
                    s_sample_count++;
                }

                if (s_step_timer_ms >= 2500) {
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.35f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 12.0f;
                    s_i50 = avg_i;
                    s_v50 = (duty_50 / 100.0f) * avg_vbus;
                    const MotorControl_t* ctrl = Motor_GetControl();
                    uint8_t pp = ctrl->pole_pairs ? ctrl->pole_pairs : 7;
                    s_rpm50 = (50.0f * 60.0f) / (float)pp;

                    s_step_timer_ms = 0;
                    s_accum_curr = 0.0f;
                    s_accum_vbus = 0.0f;
                    s_sample_count = 0;
                    s_char_state = CHAR_STATE_DYNAMIC_SPIN_100HZ;
                }
            }
            break;

        /* ====================================================================
         * Step 4b: Dynamic Spin at 100 Hz Electrical (2500 ms)
         * ==================================================================== */
        case CHAR_STATE_DYNAMIC_SPIN_100HZ:
            strncpy(s_results.status_msg, "Step 4/5: Dynamic Back-EMF Spin (100 Hz)...", sizeof(s_results.status_msg) - 1);
            {
                float freq = 100.0f;
                if (s_step_timer_ms < 500) {
                    freq = 50.0f + ((float)s_step_timer_ms / 500.0f) * 50.0f;
                }

                float duty_100 = 28.0f;
                Motor_SetSinusoidalOutput(freq, duty_100);
                IHM07M1_EnablePhase(0x07);

                if (s_step_timer_ms > 1200) {
                    s_accum_curr += telem.current_peak_amps;
                    s_accum_vbus += telem.vbus_v;
                    s_sample_count++;
                }

                if (s_step_timer_ms >= 2500) {
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.40f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 12.0f;
                    s_i100 = avg_i;
                    s_v100 = (duty_100 / 100.0f) * avg_vbus;
                    const MotorControl_t* ctrl = Motor_GetControl();
                    uint8_t pp = ctrl->pole_pairs ? ctrl->pole_pairs : 7;
                    s_rpm100 = (100.0f * 60.0f) / (float)pp;

                    s_step_timer_ms = 0;
                    s_char_state = CHAR_STATE_CALCULATING;
                }
            }
            break;

        /* ====================================================================
         * Step 5: Process Results & Compile Characterization Report
         * ==================================================================== */
        case CHAR_STATE_CALCULATING:
            /* Safe bridge shutdown */
            Motor_SetSinusoidalOutput(0.0f, 0.0f);
            IHM07M1_DisableBridge();
            IHM07M1_StopPWM();

            {
                /* Estimate Back-EMF opposing voltages */
                float e50 = s_v50 - (s_i50 * s_results.r_phase_ohm);
                float e100 = s_v100 - (s_i100 * s_results.r_phase_ohm);
                if (e50 < 0.5f) e50 = 0.5f;
                if (e100 < 1.0f) e100 = 1.0f;

                float kv = (s_rpm100 - s_rpm50) / (e100 - e50);
                if (kv < 100.0f || kv > 5000.0f) {
                    /* Fallback to single-point estimate */
                    kv = s_rpm100 / e100;
                }
                if (kv < 150.0f) kv = 150.0f;
                if (kv > 3500.0f) kv = 3500.0f;

                s_results.kv_rating = kv;
                s_results.ke_v_krpm = 1000.0f / kv;

                /* Recommend Base Duty and V/F Slope */
                float rec_base = 18.0f + (s_results.r_phase_ohm * 0.7f);
                if (rec_base < 16.0f) rec_base = 16.0f;
                if (rec_base > 26.0f) rec_base = 26.0f;

                float rec_slope = 16.0f + (1500.0f / kv) * 4.0f;
                if (rec_slope < 15.0f) rec_slope = 15.0f;
                if (rec_slope > 28.0f) rec_slope = 28.0f;

                s_results.recommended_base_duty = rec_base;
                s_results.recommended_vf_slope = rec_slope;
                s_results.valid = true;

                strncpy(s_results.status_msg, "Characterization Complete! Press [ENTER] to save.", sizeof(s_results.status_msg) - 1);
                s_char_state = CHAR_STATE_COMPLETE;
            }
            break;

        case CHAR_STATE_COMPLETE:
        case CHAR_STATE_ABORTED:
        case CHAR_STATE_IDLE:
        default:
            break;
    }
}
