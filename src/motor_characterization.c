#include "motor_characterization.h"
#include "motor_control.h"
#include "motor_profiles.h"
#include "power_stage.h"
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
    PowerStage_DisableBridge();
    PowerStage_StopPWM();
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
    /* Total duration approx 12250 ms (300ms cal + 750ms R + 1200ms align + 5000ms spin1 + 5000ms spin2) */
    if (s_char_state == CHAR_STATE_IDLE) return 0;
    if (s_char_state == CHAR_STATE_COMPLETE) return 100;
    if (s_char_state == CHAR_STATE_ABORTED) return 0;

    uint32_t pct = (s_total_timer_ms * 100) / 12250;
    if (pct > 99) pct = 99;
    return (uint8_t)pct;
}

void MotorChar_ApplyResults(void) {
    if (!s_results.valid) return;
    MotorProfiles_SaveCharacterized(
        s_results.r_phase_ohm,
        s_results.kv_rating,
        s_results.recommended_base_duty,
        s_results.recommended_vf_slope,
        s_results.recommended_load_comp,
        s_results.eddy_damping_ma_rpm
    );
    strncpy(s_results.status_msg, "Profile Saved & Activated!", sizeof(s_results.status_msg) - 1);
}

/**
 * @brief Helper to measure DC line-to-line pulse
 */
static void ApplyPulse(uint8_t phase_mask, uint16_t duty_u, uint16_t duty_v, uint16_t duty_w) {
    PowerStage_EnablePhase(phase_mask);
    PowerStage_SetPWM(duty_u, duty_v, duty_w);
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
    if (PowerStage_IsFaultActive()) {
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
            PowerStage_DisableBridge();
            PowerStage_StopPWM();
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
                PowerStage_StartPWM();
                s_char_state = CHAR_STATE_MEASURING_R_UV;
            }
            break;

        /* ====================================================================
         * Step 2a: Resistance Pulse Phase U -> V (250 ms)
         * ==================================================================== */
        case CHAR_STATE_MEASURING_R_UV:
            strncpy(s_results.status_msg, "Step 2/5: Measuring Winding Resistance...", sizeof(s_results.status_msg) - 1);
            {
                /* Target 0.60 V test pulse: safe, low power across all motor types & voltages */
                float vbus_now = (telem.vbus_v > 8.0f) ? telem.vbus_v : 24.0f;
                float test_duty_frac = 0.60f / vbus_now; /* 2.5% at 24V, 5.0% at 12V */
                if (test_duty_frac < 0.015f) test_duty_frac = 0.015f;
                if (test_duty_frac > 0.080f) test_duty_frac = 0.080f;
                uint16_t test_duty = (uint16_t)(test_duty_frac * (float)PowerStage_GetCapabilities()->pwm_arr);

                if (s_step_timer_ms < 50) {
                    ApplyPulse(0x00, 0, 0, 0); /* 50 ms settling */
                } else if (s_step_timer_ms < 220) {
                    /* U=HIGH (+), V=LOW (-), W=Hi-Z */
                    ApplyPulse(POWER_STAGE_PHASE_U_ENABLE | POWER_STAGE_PHASE_V_ENABLE, test_duty, 0, 0);
                    if (s_step_timer_ms > 100) {
                        s_accum_curr += telem.current_peak_amps;
                        s_accum_vbus += telem.vbus_v;
                        s_sample_count++;
                    }
                } else {
                    ApplyPulse(0x00, 0, 0, 0);
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.01f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 24.0f;
                    float v_applied = test_duty_frac * avg_vbus;

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
                float vbus_now = (telem.vbus_v > 8.0f) ? telem.vbus_v : 24.0f;
                float test_duty_frac = 0.60f / vbus_now;
                if (test_duty_frac < 0.015f) test_duty_frac = 0.015f;
                if (test_duty_frac > 0.080f) test_duty_frac = 0.080f;
                uint16_t test_duty = (uint16_t)(test_duty_frac * (float)PowerStage_GetCapabilities()->pwm_arr);

                if (s_step_timer_ms < 50) {
                    ApplyPulse(0x00, 0, 0, 0);
                } else if (s_step_timer_ms < 220) {
                    /* V=HIGH (+), W=LOW (-), U=Hi-Z */
                    ApplyPulse(POWER_STAGE_PHASE_V_ENABLE | POWER_STAGE_PHASE_W_ENABLE, 0, test_duty, 0);
                    if (s_step_timer_ms > 100) {
                        s_accum_curr += telem.current_peak_amps;
                        s_accum_vbus += telem.vbus_v;
                        s_sample_count++;
                    }
                } else {
                    ApplyPulse(0x00, 0, 0, 0);
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.01f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 24.0f;
                    float v_applied = test_duty_frac * avg_vbus;

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
                float vbus_now = (telem.vbus_v > 8.0f) ? telem.vbus_v : 24.0f;
                float test_duty_frac = 0.60f / vbus_now;
                if (test_duty_frac < 0.015f) test_duty_frac = 0.015f;
                if (test_duty_frac > 0.080f) test_duty_frac = 0.080f;
                uint16_t test_duty = (uint16_t)(test_duty_frac * (float)PowerStage_GetCapabilities()->pwm_arr);

                if (s_step_timer_ms < 50) {
                    ApplyPulse(0x00, 0, 0, 0);
                } else if (s_step_timer_ms < 220) {
                    /* W=HIGH (+), U=LOW (-), V=Hi-Z */
                    ApplyPulse(POWER_STAGE_PHASE_U_ENABLE | POWER_STAGE_PHASE_W_ENABLE, 0, 0, test_duty);
                    if (s_step_timer_ms > 100) {
                        s_accum_curr += telem.current_peak_amps;
                        s_accum_vbus += telem.vbus_v;
                        s_sample_count++;
                    }
                } else {
                    ApplyPulse(0x00, 0, 0, 0);
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.01f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 24.0f;
                    float v_applied = test_duty_frac * avg_vbus;

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
         * Step 3: Rotor Alignment & Settle Phase (1200 ms)
         * ==================================================================== */
        case CHAR_STATE_ALIGNMENT_STICTION:
            strncpy(s_results.status_msg, "Step 3/5: Rotor Alignment & Settle...", sizeof(s_results.status_msg) - 1);
            {
                /* Voltage-normalized alignment ramp: 0.40 V to 0.80 V target over 400 ms, then 800 ms hold */
                float vbus_now = (telem.vbus_v > 8.0f) ? telem.vbus_v : 24.0f;
                float target_v = 0.40f;
                if (s_step_timer_ms < 400) {
                    target_v = 0.40f + ((float)s_step_timer_ms / 400.0f) * 0.40f;
                } else {
                    target_v = 0.80f;
                }
                float align_duty = (target_v / vbus_now) * 100.0f;
                if (align_duty < 2.0f) align_duty = 2.0f;
                if (align_duty > 15.0f) align_duty = 15.0f;

                Motor_SetSinusoidalOutput(0.0f, align_duty);
                PowerStage_EnablePhase(POWER_STAGE_ALL_PHASES);

                if (s_step_timer_ms > 800) {
                    s_accum_curr += telem.current_peak_amps;
                    s_sample_count++;
                }

                if (s_step_timer_ms >= 1200) {
                    s_results.stiction_current_amps = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.35f;
                    s_results.min_align_duty_pct = align_duty;

                    s_step_timer_ms = 0;
                    s_accum_curr = 0.0f;
                    s_accum_vbus = 0.0f;
                    s_sample_count = 0;
                    s_char_state = CHAR_STATE_DYNAMIC_SPIN_50HZ;
                }
            }
            break;

        /* ====================================================================
         * Step 4a: Dynamic Spin Point 1 (25 Hz Electrical / ~214 or ~107 RPM) (5000 ms)
         * Purpose: Spin motor up smoothly through stiction and accelerate the copper
         * disc flywheel up to 25 Hz electrical frequency. Once steady state is
         * reached, sample bus voltage and active phase current.
         * ==================================================================== */
        case CHAR_STATE_DYNAMIC_SPIN_50HZ:
            strncpy(s_results.status_msg, "Step 4/5: Dynamic Spin 1 (25 Hz Electrical)...", sizeof(s_results.status_msg) - 1);
            {
                /* Smooth 3000 ms ramp from 0.5 -> 25 Hz (~35 RPM/s slew rate for 7 PP),
                 * followed by 2000 ms steady-state measurement window */
                float freq = 25.0f;
                if (s_step_timer_ms < 3000) {
                    freq = 0.5f + ((float)s_step_timer_ms / 3000.0f) * 24.5f;
                }

                float vbus_now = (telem.vbus_v > 8.0f) ? telem.vbus_v : 24.0f;
                /* Applied voltage profile: 1.80V baseline boost ramping to 2.40V at 25 Hz.
                 * Provides sufficient torque margin to overcome copper disc inertia and
                 * low-speed eddy brake drag without stalling. */
                float target_v_25 = 2.00f + (freq / 25.0f) * 0.80f; /* 2.00V to 2.80V */
                float duty_25 = (target_v_25 / vbus_now) * 100.0f;
                if (duty_25 < 5.0f) duty_25 = 5.0f;
                if (duty_25 > 25.0f) duty_25 = 25.0f;

                Motor_SetSinusoidalOutput(freq, duty_25);
                PowerStage_EnablePhase(POWER_STAGE_ALL_PHASES);

                /* Collect steady-state current and voltage samples after acceleration ramp finishes */
                if (s_step_timer_ms > 3200) {
                    s_accum_curr += telem.current_peak_amps;
                    s_accum_vbus += telem.vbus_v;
                    s_sample_count++;
                }

                if (s_step_timer_ms >= 5000) {
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.35f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 24.0f;
                    s_i50 = avg_i;
                    s_v50 = (duty_25 / 100.0f) * avg_vbus;
                    const MotorControl_t* ctrl = Motor_GetControl();
                    uint8_t pp = (ctrl && ctrl->pole_pairs) ? ctrl->pole_pairs : 7;
                    s_rpm50 = (25.0f * 60.0f) / (float)pp;

                    s_step_timer_ms = 0;
                    s_accum_curr = 0.0f;
                    s_accum_vbus = 0.0f;
                    s_sample_count = 0;
                    s_char_state = CHAR_STATE_DYNAMIC_SPIN_100HZ;
                }
            }
            break;

        /* ====================================================================
         * Step 4b: Dynamic Spin Point 2 (50 Hz Electrical / ~428 or ~214 RPM) (5000 ms)
         * Purpose: Accelerate motor from 25 Hz to 50 Hz electrical. Under eddy
         * current braking, the opposing torque doubles between Point 1 and Point 2
         * (tau_eddy = c * omega). Measuring the delta in current between Point 1
         * and Point 2 directly yields the eddy current drag slope.
         * ==================================================================== */
        case CHAR_STATE_DYNAMIC_SPIN_100HZ:
            strncpy(s_results.status_msg, "Step 4/5: Dynamic Spin 2 (50 Hz Electrical)...", sizeof(s_results.status_msg) - 1);
            {
                /* Smooth 3000 ms ramp from 25 -> 50 Hz (~35 RPM/s slew rate for 7 PP),
                 * followed by 2000 ms steady-state measurement window */
                float freq = 50.0f;
                if (s_step_timer_ms < 3000) {
                    freq = 25.0f + ((float)s_step_timer_ms / 3000.0f) * 25.0f;
                }

                float vbus_now = (telem.vbus_v > 8.0f) ? telem.vbus_v : 24.0f;
                /* Applied voltage profile: 2.80V at 25 Hz ramping to 3.80V at 50 Hz.
                 * The additional headroom ensures the motor delivers the increased
                 * torque required by the eddy current brake at 50 Hz. */
                float target_v_50 = 2.80f + ((freq - 25.0f) / 25.0f) * 1.00f; /* 2.80V to 3.80V */
                float duty_50 = (target_v_50 / vbus_now) * 100.0f;
                if (duty_50 < 7.0f) duty_50 = 7.0f;
                if (duty_50 > 30.0f) duty_50 = 30.0f;

                Motor_SetSinusoidalOutput(freq, duty_50);
                PowerStage_EnablePhase(POWER_STAGE_ALL_PHASES);

                /* Collect steady-state current and voltage samples after ramp completes */
                if (s_step_timer_ms > 3200) {
                    s_accum_curr += telem.current_peak_amps;
                    s_accum_vbus += telem.vbus_v;
                    s_sample_count++;
                }

                if (s_step_timer_ms >= 5000) {
                    float avg_i = (s_sample_count > 0) ? (s_accum_curr / (float)s_sample_count) : 0.40f;
                    float avg_vbus = (s_sample_count > 0) ? (s_accum_vbus / (float)s_sample_count) : 24.0f;
                    s_i100 = avg_i;
                    s_v100 = (duty_50 / 100.0f) * avg_vbus;
                    const MotorControl_t* ctrl = Motor_GetControl();
                    uint8_t pp = (ctrl && ctrl->pole_pairs) ? ctrl->pole_pairs : 7;
                    s_rpm100 = (50.0f * 60.0f) / (float)pp;

                    s_step_timer_ms = 0;
                    s_char_state = CHAR_STATE_CALCULATING;
                }
            }
            break;

        /* ====================================================================
         * Step 5: Process Results, Calculate Parameters & Compile Report
         * ==================================================================== */
        case CHAR_STATE_CALCULATING:
            /* Safe bridge shutdown: disable all half bridges */
            Motor_SetSinusoidalOutput(0.0f, 0.0f);
            PowerStage_DisableBridge();
            PowerStage_StopPWM();

            {
                /* 1. Estimate Back-EMF opposing voltages: e = V_applied - (I_phase * Rs * 1.5) */
                float e1 = s_v50 - (s_i50 * s_results.r_phase_ohm * 1.5f);
                float e2 = s_v100 - (s_i100 * s_results.r_phase_ohm * 1.5f);
                if (e1 < 0.2f) e1 = 0.2f;
                if (e2 < 0.5f) e2 = 0.5f;

                /* 2. Compute Motor Velocity Constant Kv = Delta(RPM) / Delta(E_bemf) */
                float kv = 360.0f;
                if ((e2 - e1) > 0.15f && (s_rpm100 - s_rpm50) > 30.0f) {
                    kv = (s_rpm100 - s_rpm50) / (e2 - e1);
                } else if (e2 > 0.3f) {
                    kv = s_rpm100 / e2;
                }

                if (kv < 120.0f) kv = 120.0f;
                if (kv > 2500.0f) kv = 2500.0f;

                s_results.kv_rating = kv;
                s_results.ke_v_krpm = 1000.0f / kv;

                /* 3. Characterize Eddy Current Brake Damping Slope:
                 * k_eddy = Delta(I_phase) / Delta(RPM) in mA / RPM
                 * Physical model: tau_eddy = c * omega -> I_q = tau_eddy / Kt -> I_phase proportional to RPM */
                float delta_rpm = s_rpm100 - s_rpm50;
                float delta_i_ma = (s_i100 - s_i50) * 1000.0f;
                float k_eddy = 0.0f;
                if (delta_rpm > 20.0f && delta_i_ma > 0.0f) {
                    k_eddy = delta_i_ma / delta_rpm;
                }
                if (k_eddy > 5.0f) k_eddy = 5.0f; /* Safe ceiling clamp */
                s_results.eddy_damping_ma_rpm = k_eddy;

                /* 4. Recommend IR Load Compensation Gain:
                 * In scalar V/F drive, load current creates an internal stator impedance
                 * drop (I * Rs + omega_e * Ls * I). Under eddy braking, current grows with
                 * speed. Providing an active load compensation boost:
                 *   V_target = V_boost + (RPM / Kv) + K_comp * I_phase * Rs
                 * prevents the rotor from dropping behind the synchronous rotating field. */
                float rec_load_comp = 1.00f + (k_eddy * 0.40f);
                if (rec_load_comp < 0.50f) rec_load_comp = 0.50f;
                if (rec_load_comp > 2.50f) rec_load_comp = 2.50f;
                s_results.recommended_load_comp = rec_load_comp;

                /* 5. Recommend Base Duty and V/F Slope scaled to active bus voltage */
                float vbus_now = (telem.vbus_v > 8.0f) ? telem.vbus_v : 24.0f;
                float rec_base = (1.60f / vbus_now) * 100.0f; /* 1.60V baseline starting boost for flywheel */
                if (rec_base < 5.0f) rec_base = 5.0f;
                if (rec_base > 18.0f) rec_base = 18.0f;

                float rec_slope = (1000.0f / kv) * (100.0f / vbus_now) * 2.5f;
                if (rec_slope < 15.0f) rec_slope = 15.0f;
                if (rec_slope > 50.0f) rec_slope = 50.0f;

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
