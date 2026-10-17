#include "motor_control.h"
#include "motor_profiles.h"
#include "motor_characterization.h"
#include "power_stage.h"
#include <math.h>

#define PI_FLOAT                    3.14159265358979323846f
#define TWO_PI                      (2.0f * PI_FLOAT)

static MotorControl_t g_motor_ctrl;
static MotorTelemetry_t g_telemetry;

/* Motor initialization flag to prevent SysTick ISR preemption before init completes */
static volatile bool s_motor_initialized = false;

/* 256-point Q12 fixed-point sinusoidal modulation table: (0.5 + 0.5*sin(theta)) * 4096 */
static const uint16_t s_sin_table_q12[256] = {
    2048, 2098, 2148, 2199, 2249, 2299, 2349, 2398,
    2448, 2497, 2546, 2594, 2643, 2690, 2738, 2785,
    2832, 2878, 2924, 2969, 3013, 3057, 3101, 3144,
    3186, 3227, 3268, 3308, 3347, 3386, 3423, 3460,
    3496, 3531, 3565, 3599, 3631, 3663, 3693, 3722,
    3751, 3778, 3805, 3830, 3854, 3877, 3899, 3920,
    3940, 3959, 3976, 3993, 4008, 4022, 4035, 4046,
    4057, 4066, 4074, 4081, 4086, 4090, 4094, 4095,
    4096, 4095, 4094, 4090, 4086, 4081, 4074, 4066,
    4057, 4046, 4035, 4022, 4008, 3993, 3976, 3959,
    3940, 3920, 3899, 3877, 3854, 3830, 3805, 3778,
    3751, 3722, 3693, 3663, 3631, 3599, 3565, 3531,
    3496, 3460, 3423, 3386, 3347, 3308, 3268, 3227,
    3186, 3144, 3101, 3057, 3013, 2969, 2924, 2878,
    2832, 2785, 2738, 2690, 2643, 2594, 2546, 2497,
    2448, 2398, 2349, 2299, 2249, 2199, 2148, 2098,
    2048, 1998, 1948, 1897, 1847, 1797, 1747, 1698,
    1648, 1599, 1550, 1502, 1453, 1406, 1358, 1311,
    1264, 1218, 1172, 1127, 1083, 1039,  995,  952,
     910,  869,  828,  788,  749,  710,  673,  636,
     600,  565,  531,  497,  465,  433,  403,  374,
     345,  318,  291,  266,  242,  219,  197,  176,
     156,  137,  120,  103,   88,   74,   61,   50,
      39,   30,   22,   15,   10,    6,    2,    1,
       0,    1,    2,    6,   10,   15,   22,   30,
      39,   50,   61,   74,   88,  103,  120,  137,
     156,  176,  197,  219,  242,  266,  291,  318,
     345,  374,  403,  433,  465,  497,  531,  565,
     600,  636,  673,  710,  749,  788,  828,  869,
     910,  952,  995, 1039, 1083, 1127, 1172, 1218,
    1264, 1311, 1358, 1406, 1453, 1502, 1550, 1599,
    1648, 1698, 1747, 1797, 1847, 1897, 1948, 1998
};

/* 32-bit fixed point phase accumulator (top 8 bits = index into 256-point table) */
static volatile uint32_t s_phase_accum = 0;
static volatile int32_t s_phase_step = 0;
static volatile uint32_t s_active_ccr_scale = 0;
static float g_open_loop_angle = 0.0f;

/* Quiescent zero-current ADC offsets (measured at boot when bridge is off) */
static uint16_t g_raw_offset_a = 2048;
static uint16_t g_raw_offset_b = 2048;
static uint16_t g_raw_offset_c = 2048;

void Motor_CalibrateCurrentOffsets(void) {
    PowerStage_CalibrateCurrentOffsets(&g_raw_offset_a, &g_raw_offset_b, &g_raw_offset_c);
}

void Motor_Init(void) {
    s_motor_initialized = false;
    g_motor_ctrl.mode = MOTOR_MODE_OPEN_LOOP_CHAR;
    g_motor_ctrl.state = MOTOR_STATE_STOPPED;
    g_motor_ctrl.direction = 1;
    g_motor_ctrl.active_duty_pct = 0.0f;
    g_motor_ctrl.run_time_ms = 0;
    g_motor_ctrl.startup_timer_ms = 0;
    g_motor_ctrl.current_freq_hz = 0.0f;
    g_motor_ctrl.pot_control_enabled = true;

    PowerStage_Init();
    PowerStage_DisableBridge();

    /* Initialize motor profiles and apply default profile to drive */
    MotorProfiles_Init();

    /* Read initial potentiometer position (0 - 100%) and set target speed */
    g_telemetry.pot_pct = PowerStage_ReadPotentiometer();
    float min_s = (g_motor_ctrl.min_rpm > 50.0f) ? g_motor_ctrl.min_rpm : MOTOR_POT_MIN_RPM;
    float max_s = (g_motor_ctrl.max_rpm > min_s) ? g_motor_ctrl.max_rpm : MOTOR_POT_MAX_RPM;
    float init_rpm = min_s + (g_telemetry.pot_pct / 100.0f) * (max_s - min_s);
    Motor_SetSpeed(init_rpm);

    /* Measure zero-current ADC quiescent offsets */
    Motor_CalibrateCurrentOffsets();

    /* Initialization complete: allow SysTick ISR to execute Motor_Update1ms */
    s_motor_initialized = true;
}

void Motor_ApplyProfile(const MotorProfile_t* prof) {
    if (!prof) return;
    g_motor_ctrl.pole_pairs = prof->pole_pairs;
    g_motor_ctrl.duty_pct = prof->base_duty_pct;
    g_motor_ctrl.vf_slope_pct = prof->vf_slope_pct;
    g_motor_ctrl.accel_slew_rpm_s = prof->accel_slew_rpm_s;
    g_motor_ctrl.min_rpm = prof->min_rpm;
    g_motor_ctrl.max_rpm = prof->max_rpm;
    g_motor_ctrl.torque_limit_amps = prof->max_continuous_amps;
    g_motor_ctrl.current_limit_amps = prof->hw_break_trip_amps;

    /* Re-evaluate target speed based on pot position and new RPM range */
    float min_s = g_motor_ctrl.min_rpm;
    float max_s = g_motor_ctrl.max_rpm;
    float new_target_rpm = min_s + (g_telemetry.pot_pct / 100.0f) * (max_s - min_s);
    Motor_SetSpeed(new_target_rpm);

    /* Update hardware current trip threshold */
    PowerStage_SetCurrentTripThreshold(prof->hw_break_trip_amps);
}

void Motor_StartCharacterization(void) {
    Motor_ResetFault();
    PowerStage_ClearFault();
    PowerStage_DisableBridge();
    PowerStage_StopPWM();
    g_motor_ctrl.actual_speed_rpm = 0.0f;
    g_motor_ctrl.current_freq_hz = 0.0f;
    s_phase_step = 0;
    s_active_ccr_scale = 0;
    g_motor_ctrl.state = MOTOR_STATE_CHARACTERIZING;
    MotorChar_Start();
}

void Motor_SetTorqueLimit(float amps) {
    const PowerStageCapabilities_t* caps = PowerStage_GetCapabilities();
    float max_a = caps ? caps->max_continuous_current_a : 1.4f;
    if (amps < 0.3f) amps = 0.3f;
    if (amps > max_a) amps = max_a;
    g_motor_ctrl.torque_limit_amps = amps;
}

void Motor_IncTorqueLimit(void) {
    Motor_SetTorqueLimit(g_motor_ctrl.torque_limit_amps + 0.1f);
}

void Motor_DecTorqueLimit(void) {
    Motor_SetTorqueLimit(g_motor_ctrl.torque_limit_amps - 0.1f);
}

void Motor_TogglePotControl(void) {
    g_motor_ctrl.pot_control_enabled = !g_motor_ctrl.pot_control_enabled;
}

void Motor_SetPotControl(bool enable) {
    g_motor_ctrl.pot_control_enabled = enable;
}

void Motor_Start(void) {
    if (g_motor_ctrl.state == MOTOR_STATE_FAULT) {
        Motor_ResetFault();
    }
    if (g_motor_ctrl.state == MOTOR_STATE_STOPPED) {
        g_motor_ctrl.state = MOTOR_STATE_STARTING;
        g_motor_ctrl.startup_timer_ms = 0;
        g_motor_ctrl.current_freq_hz = 0.0f;
        g_motor_ctrl.actual_speed_rpm = 0.0f;
        g_open_loop_angle = 0.0f;
        s_phase_accum = 0;
        s_phase_step = 0;
        s_active_ccr_scale = (uint32_t)((g_motor_ctrl.duty_pct / 100.0f) * (float)PowerStage_GetCapabilities()->pwm_arr);
        PowerStage_EnablePhase(POWER_STAGE_ALL_PHASES);
        PowerStage_StartPWM();
        PowerStage_SetStatusLED(false);
    }
}

void Motor_Stop(void) {
    if (g_motor_ctrl.state == MOTOR_STATE_CHARACTERIZING) {
        MotorChar_Abort("Stopped by user.");
    }
    g_motor_ctrl.state = MOTOR_STATE_STOPPED;
    g_motor_ctrl.startup_timer_ms = 0;
    g_motor_ctrl.current_freq_hz = 0.0f;
    g_motor_ctrl.actual_speed_rpm = 0.0f;
    s_phase_step = 0;
    s_active_ccr_scale = 0;
    PowerStage_DisableBridge();
    PowerStage_StopPWM();
}

void Motor_Brake(void) {
    if (g_motor_ctrl.state == MOTOR_STATE_RUNNING) {
        g_motor_ctrl.state = MOTOR_STATE_BRAKING;
    }
}

void Motor_SetDuty(float duty_pct) {
    if (duty_pct < 5.0f) duty_pct = 5.0f;
    if (duty_pct > 75.0f) duty_pct = 75.0f; /* Clamp to safe 75% maximum */
    g_motor_ctrl.duty_pct = duty_pct;
}

void Motor_IncDuty(void) {
    Motor_SetDuty(g_motor_ctrl.duty_pct + 1.0f);
}

void Motor_DecDuty(void) {
    Motor_SetDuty(g_motor_ctrl.duty_pct - 1.0f);
}

void Motor_SetSpeed(float rpm) {
    if (rpm < 0.0f) rpm = 0.0f;
    if (rpm > 6000.0f) rpm = 6000.0f;
    g_motor_ctrl.target_speed_rpm = rpm;
    /* Electrical Hz = (RPM * pole_pairs) / 60 */
    g_motor_ctrl.electrical_freq_hz = (rpm * (float)g_motor_ctrl.pole_pairs) / 60.0f;
}

void Motor_SetDirection(int8_t dir) {
    if (dir >= 0) {
        g_motor_ctrl.direction = 1;
    } else {
        g_motor_ctrl.direction = -1;
    }
}

void Motor_SetMode(MotorMode_t mode) {
    Motor_Stop();
    g_motor_ctrl.mode = mode;
}

void Motor_ResetFault(void) {
    PowerStage_ClearFault();
    PowerStage_SetStatusLED(false);
    if (g_motor_ctrl.state == MOTOR_STATE_FAULT) {
        g_motor_ctrl.state = MOTOR_STATE_STOPPED;
    }
}

/**
 * @brief Periodic 1 ms Motor Control & Telemetry Execution Loop
 */
void Motor_Update1ms(void) {
    if (!s_motor_initialized) {
        return;
    }

    g_telemetry.sample_count++;

    /* 1. Hardware Fault Check */
    if (PowerStage_IsFaultActive()) {
        g_motor_ctrl.state = MOTOR_STATE_FAULT;
        g_telemetry.fault_active = true;
        PowerStage_DisableBridge();
        PowerStage_SetStatusLED(true);
        return;
    }
    g_telemetry.fault_active = false;

    const PowerStageCapabilities_t* caps = PowerStage_GetCapabilities();

    /* 2. Read Telemetry Sensors */
    /* Sample slow telemetry channels periodically (every 50 ms) to minimize ADC overhead */
    static uint8_t s_slow_telem_div = 0;
    if (++s_slow_telem_div >= 50) {
        s_slow_telem_div = 0;
        g_telemetry.vbus_v = PowerStage_ReadVBusVoltage();
        g_telemetry.pot_pct = PowerStage_ReadPotentiometer();
        g_telemetry.temp_c = PowerStage_ReadTemperature();
    }

    /* Current Shunts with Calibrated Zero-Current Baseline Subtraction */
    uint16_t raw_ia = 0, raw_ib = 0, raw_ic = 0;
    PowerStage_ReadRawCurrentSensors(&raw_ia, &raw_ib, &raw_ic);

    int32_t diff_a = (int32_t)raw_ia - (int32_t)g_raw_offset_a;
    int32_t diff_b = (int32_t)raw_ib - (int32_t)g_raw_offset_b;
    int32_t diff_c = (int32_t)raw_ic - (int32_t)g_raw_offset_c;

    if (diff_a < 0) diff_a = -diff_a;
    if (diff_b < 0) diff_b = -diff_b;
    if (diff_c < 0) diff_c = -diff_c;

    float gain = (caps && caps->current_sense_gain > 0.001f) ? caps->current_sense_gain : 0.5049f;
    g_telemetry.current_a_amps = (((float)diff_a / 4095.0f) * 3.3f) / gain;
    g_telemetry.current_b_amps = (((float)diff_b / 4095.0f) * 3.3f) / gain;
    g_telemetry.current_c_amps = (((float)diff_c / 4095.0f) * 3.3f) / gain;

    float p_curr = g_telemetry.current_a_amps;
    if (g_telemetry.current_b_amps > p_curr) p_curr = g_telemetry.current_b_amps;
    if (g_telemetry.current_c_amps > p_curr) p_curr = g_telemetry.current_c_amps;
    g_telemetry.current_peak_amps = p_curr;

    /* Hall Sensors */
    g_telemetry.hall_state = PowerStage_ReadHallSensors();

    /* If characterizing, delegate to characterization engine */
    if (g_motor_ctrl.state == MOTOR_STATE_CHARACTERIZING) {
        MotorChar_Update1ms();
        return;
    }

    /* 3. Motor State Handling */
    float min_s = (g_motor_ctrl.min_rpm > 50.0f) ? g_motor_ctrl.min_rpm : MOTOR_POT_MIN_RPM;
    float max_s = (g_motor_ctrl.max_rpm > min_s) ? g_motor_ctrl.max_rpm : MOTOR_POT_MAX_RPM;
    float vf_slope = (g_motor_ctrl.vf_slope_pct > 0.0f) ? g_motor_ctrl.vf_slope_pct : 32.0f;

    if (g_motor_ctrl.pot_control_enabled) {
        float pot_demanded_rpm = min_s + (g_telemetry.pot_pct / 100.0f) * (max_s - min_s);
        if (g_motor_ctrl.state == MOTOR_STATE_STOPPED) {
            g_motor_ctrl.target_speed_rpm = pot_demanded_rpm;
            g_motor_ctrl.electrical_freq_hz = (pot_demanded_rpm * (float)g_motor_ctrl.pole_pairs) / 60.0f;
        } else if (g_motor_ctrl.state == MOTOR_STATE_RUNNING) {
            /* Slew target speed toward pot demanded speed */
            float speed_diff = pot_demanded_rpm - g_motor_ctrl.target_speed_rpm;
            float max_slew_per_ms = (g_motor_ctrl.accel_slew_rpm_s > 10.0f) ? (g_motor_ctrl.accel_slew_rpm_s / 1000.0f) : 0.15f;
            if (fabsf(speed_diff) <= max_slew_per_ms) {
                g_motor_ctrl.target_speed_rpm = pot_demanded_rpm;
            } else if (speed_diff > 0.0f) {
                g_motor_ctrl.target_speed_rpm += max_slew_per_ms;
            } else {
                g_motor_ctrl.target_speed_rpm -= max_slew_per_ms;
            }
            g_motor_ctrl.electrical_freq_hz = (g_motor_ctrl.target_speed_rpm * (float)g_motor_ctrl.pole_pairs) / 60.0f;
        }
    }

    if (g_motor_ctrl.state == MOTOR_STATE_STOPPED) {
        PowerStage_DisableBridge();
        g_motor_ctrl.actual_speed_rpm = 0.0f;
        g_motor_ctrl.current_freq_hz = 0.0f;
        s_phase_step = 0;
        s_active_ccr_scale = 0;
        return;
    }

    if (g_motor_ctrl.state == MOTOR_STATE_BRAKING) {
        /* Active controlled deceleration */
        if (g_motor_ctrl.duty_pct > 2.0f) {
            g_motor_ctrl.duty_pct -= 0.2f; /* Smooth deceleration slope */
        } else {
            Motor_Stop();
            return;
        }
    }

    g_motor_ctrl.run_time_ms++;

    /* 4. Determine Active Frequency and Commanded Duty */
    float active_freq = 0.0f;
    float duty_bemf = g_motor_ctrl.duty_pct;

    if (g_motor_ctrl.state == MOTOR_STATE_STARTING) {
        g_motor_ctrl.startup_timer_ms++;

        if (g_motor_ctrl.startup_timer_ms < 500) {
            /* 4.1 DC Alignment Phase (500 ms): Firmly lock rotor to electrical angle 0 */
            active_freq = 0.0f;
            s_phase_accum = 0;
            s_phase_step = 0;
            g_open_loop_angle = 0.0f;
            g_motor_ctrl.actual_speed_rpm = 0.0f;
            g_motor_ctrl.current_freq_hz = 0.0f;
            duty_bemf = g_motor_ctrl.duty_pct + 2.0f; /* Solid stiction-breaking alignment torque */
        } else if (g_motor_ctrl.startup_timer_ms < (500 + 3500)) {
            /* 4.2 Smooth Acceleration Ramp (3.5 seconds): 2 Hz -> f_target */
            uint32_t ramp_elapsed = g_motor_ctrl.startup_timer_ms - 500;
            float progress = (float)ramp_elapsed / 3500.0f;
            float f_start = 2.0f;
            float f_target = g_motor_ctrl.electrical_freq_hz;
            active_freq = f_start + progress * (f_target - f_start);
            g_motor_ctrl.current_freq_hz = active_freq;
            g_motor_ctrl.actual_speed_rpm = (active_freq * 60.0f) / (float)g_motor_ctrl.pole_pairs;

            /* Automatic V/F Back-EMF tracking based on profile slope */
            float speed_ratio = (g_motor_ctrl.actual_speed_rpm - min_s) / (max_s - min_s);
            if (speed_ratio < 0.0f) speed_ratio = 0.0f;
            if (speed_ratio > 1.0f) speed_ratio = 1.0f;
            duty_bemf = g_motor_ctrl.duty_pct + (speed_ratio * vf_slope);
        } else {
            /* Ramp complete: transition smoothly to RUNNING */
            g_motor_ctrl.state = MOTOR_STATE_RUNNING;
            active_freq = g_motor_ctrl.electrical_freq_hz;
            g_motor_ctrl.current_freq_hz = active_freq;
            g_motor_ctrl.actual_speed_rpm = (active_freq * 60.0f) / (float)g_motor_ctrl.pole_pairs;

            float speed_ratio = (g_motor_ctrl.actual_speed_rpm - min_s) / (max_s - min_s);
            if (speed_ratio < 0.0f) speed_ratio = 0.0f;
            if (speed_ratio > 1.0f) speed_ratio = 1.0f;
            duty_bemf = g_motor_ctrl.duty_pct + (speed_ratio * vf_slope);
        }
    } else if (g_motor_ctrl.state == MOTOR_STATE_RUNNING || g_motor_ctrl.state == MOTOR_STATE_BRAKING) {
        active_freq = g_motor_ctrl.electrical_freq_hz;
        g_motor_ctrl.current_freq_hz = active_freq;
        g_motor_ctrl.actual_speed_rpm = (active_freq * 60.0f) / (float)g_motor_ctrl.pole_pairs;

        /* Automatic V/F Back-EMF tracking based on profile slope */
        float speed_ratio = (g_motor_ctrl.actual_speed_rpm - min_s) / (max_s - min_s);
        if (speed_ratio < 0.0f) speed_ratio = 0.0f;
        if (speed_ratio > 1.0f) speed_ratio = 1.0f;
        duty_bemf = g_motor_ctrl.duty_pct + (speed_ratio * vf_slope);
    }

    /* 4.3 Filtered Current Monitoring & Sustained Stall Protection */
    static float s_filt_current = 0.0f;
    static uint16_t s_overcurrent_timer_ms = 0;

    if (g_motor_ctrl.state == MOTOR_STATE_STOPPED) {
        s_filt_current = 0.0f;
        s_overcurrent_timer_ms = 0;
    } else {
        /* Single-pole IIR low-pass filter (cutoff ~6 Hz at 1 kHz sample rate) to reject switching noise */
        s_filt_current = (s_filt_current * 0.96f) + (g_telemetry.current_peak_amps * 0.04f);

        /* If sustained current exceeds torque_limit_amps for >1.5 seconds, shut down (stall) */
        if (s_filt_current > g_motor_ctrl.torque_limit_amps) {
            s_overcurrent_timer_ms++;
            if (s_overcurrent_timer_ms > 1500) {
                g_motor_ctrl.state = MOTOR_STATE_FAULT;
                g_telemetry.fault_active = true;
                PowerStage_DisableBridge();
                PowerStage_SetStatusLED(true);
                return;
            }
        } else if (s_overcurrent_timer_ms > 0) {
            s_overcurrent_timer_ms--;
        }
    }

    /* 4.4 Dynamic Torque Limiting & Current Foldback Regulator */
    float duty_torque_adjust = 0.0f;
    float current_thresh = g_motor_ctrl.torque_limit_amps * 0.75f;
    if (s_filt_current > current_thresh) {
        /* Proportional foldback: up to 15% duty reduction per 1.0 A excess current */
        float over_i = s_filt_current - current_thresh;
        duty_torque_adjust = over_i * 15.0f;
        if (duty_torque_adjust > (duty_bemf - 4.0f)) {
            duty_torque_adjust = duty_bemf - 4.0f;
        }
    }

    float active_duty = duty_bemf - duty_torque_adjust;

    /* 4.5 Supply Voltage Feedforward Scaling: Scale duty inversely with bus voltage */
    float vbus = (g_telemetry.vbus_v > 8.0f) ? g_telemetry.vbus_v : 12.0f;
    const MotorProfile_t* active_prof = MotorProfiles_GetActive();
    float rated_v = (active_prof && active_prof->rated_voltage_v > 8.0f) ? active_prof->rated_voltage_v : 12.0f;
    float v_scale = rated_v / vbus;
    if (v_scale > 1.5f) v_scale = 1.5f;
    if (v_scale < 0.35f) v_scale = 0.35f;

    float scaled_active_duty = active_duty * v_scale;
    float min_duty_clamp = 4.0f * (12.0f / vbus);
    if (min_duty_clamp < 2.0f) min_duty_clamp = 2.0f;
    if (scaled_active_duty < min_duty_clamp) scaled_active_duty = min_duty_clamp;
    if (scaled_active_duty > 75.0f) scaled_active_duty = 75.0f;
    g_motor_ctrl.active_duty_pct = scaled_active_duty;

    /* Update parameters for the ultrafast 20 kHz TIM1 Update ISR */
    uint16_t arr = (caps && caps->pwm_arr) ? caps->pwm_arr : 4249;
    s_active_ccr_scale = (uint32_t)((scaled_active_duty / 100.0f) * (float)arr);

    /* Phase step per 20 kHz PWM cycle: step = (freq * 2^32) / 20000 = freq * 214748.3648 */
    int32_t step = (int32_t)(active_freq * 214748.3648f);
    if (g_motor_ctrl.direction < 0) {
        step = -step;
    }
    s_phase_step = step;

    /* Expose current electrical angle in radians for telemetry / 6-step */
    g_open_loop_angle = ((float)(s_phase_accum >> 16) / 65536.0f) * TWO_PI;

    /* 5. Phase Hardware Enable Control */
    if (g_motor_ctrl.mode == MOTOR_MODE_OPEN_LOOP_CHAR) {
        PowerStage_EnablePhase(POWER_STAGE_ALL_PHASES); /* Enable all 3 half bridges for 20 kHz sinusoidal drive */
    } else if (g_motor_ctrl.mode == MOTOR_MODE_SIX_STEP_VOLTAGE) {
        /* 6-step trapezoidal commutation */
        uint8_t step_num = (uint8_t)((g_open_loop_angle / TWO_PI) * 6.0f) + 1;
        if (step_num > 6) step_num = 6;

        uint16_t pwm_val = (uint16_t)((scaled_active_duty / 100.0f) * (float)arr);

        switch (step_num) {
            case 1:
                PowerStage_EnablePhase(POWER_STAGE_PHASE_U_ENABLE | POWER_STAGE_PHASE_V_ENABLE);
                PowerStage_SetPWM(pwm_val, 0, 0);
                break;
            case 2:
                PowerStage_EnablePhase(POWER_STAGE_PHASE_U_ENABLE | POWER_STAGE_PHASE_W_ENABLE);
                PowerStage_SetPWM(pwm_val, 0, 0);
                break;
            case 3:
                PowerStage_EnablePhase(POWER_STAGE_PHASE_V_ENABLE | POWER_STAGE_PHASE_W_ENABLE);
                PowerStage_SetPWM(0, pwm_val, 0);
                break;
            case 4:
                PowerStage_EnablePhase(POWER_STAGE_PHASE_U_ENABLE | POWER_STAGE_PHASE_V_ENABLE);
                PowerStage_SetPWM(0, pwm_val, 0);
                break;
            case 5:
                PowerStage_EnablePhase(POWER_STAGE_PHASE_U_ENABLE | POWER_STAGE_PHASE_W_ENABLE);
                PowerStage_SetPWM(0, 0, pwm_val);
                break;
            case 6:
                PowerStage_EnablePhase(POWER_STAGE_PHASE_V_ENABLE | POWER_STAGE_PHASE_W_ENABLE);
                PowerStage_SetPWM(0, 0, pwm_val);
                break;
            default:
                PowerStage_DisableBridge();
                break;
        }
    }
}

/**
 * @brief Dynamic sinusoidal generator helper for characterization
 */
void Motor_SetSinusoidalOutput(float freq_hz, float duty_pct) {
    if (duty_pct <= 0.0f) {
        s_phase_step = 0;
        s_active_ccr_scale = 0;
        return;
    }
    const PowerStageCapabilities_t* caps = PowerStage_GetCapabilities();
    uint16_t arr = (caps && caps->pwm_arr) ? caps->pwm_arr : 4249;
    s_active_ccr_scale = (uint32_t)((duty_pct / 100.0f) * (float)arr);
    int32_t step = (int32_t)(freq_hz * 214748.3648f);
    if (g_motor_ctrl.direction < 0) {
        step = -step;
    }
    s_phase_step = step;
}

/**
 * @brief 20 kHz TIM1 PWM Update ISR (Ultra-Fast Fixed-Point Sine Modulation)
 *        Takes ~15 clock cycles (0.088 us, <0.2% CPU load at 20 kHz) with zero floating point library overhead!
 */
void Motor_PWM_UpdateISR(void) {
    if (g_motor_ctrl.state == MOTOR_STATE_RUNNING || 
        g_motor_ctrl.state == MOTOR_STATE_STARTING ||
        g_motor_ctrl.state == MOTOR_STATE_CHARACTERIZING) {
        if (g_motor_ctrl.mode == MOTOR_MODE_OPEN_LOOP_CHAR) {
            s_phase_accum += (uint32_t)s_phase_step;

            uint8_t u_idx = (uint8_t)(s_phase_accum >> 24);
            uint8_t v_idx = (uint8_t)(u_idx + 85);   /* +120 deg */
            uint8_t w_idx = (uint8_t)(u_idx + 171);  /* +240 deg */

            uint32_t scale = s_active_ccr_scale;
            htim1.Instance->CCR1 = (uint32_t)((s_sin_table_q12[u_idx] * scale) >> 12);
            htim1.Instance->CCR2 = (uint32_t)((s_sin_table_q12[v_idx] * scale) >> 12);
            htim1.Instance->CCR3 = (uint32_t)((s_sin_table_q12[w_idx] * scale) >> 12);
        }
    }
}

void Motor_GetTelemetry(MotorTelemetry_t* telem) {
    if (telem) {
        *telem = g_telemetry;
    }
}

const MotorControl_t* Motor_GetControl(void) {
    return &g_motor_ctrl;
}
