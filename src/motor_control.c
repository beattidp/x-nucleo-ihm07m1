#include "motor_control.h"
#include "ihm07m1.h"
#include <math.h>

#define PI_FLOAT                    3.14159265358979323846f
#define TWO_PI                      (2.0f * PI_FLOAT)

static MotorControl_t g_motor_ctrl;
static MotorTelemetry_t g_telemetry;

/* Open-loop electrical angle accumulator (radians) */
static float g_open_loop_angle = 0.0f;

/* Read a single ADC channel synchronously */
static uint16_t ReadADCChannel(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 5) == HAL_OK) {
        uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
        return val;
    }
    HAL_ADC_Stop(&hadc1);
    return 0;
}

void Motor_Init(void) {
    g_motor_ctrl.mode = MOTOR_MODE_OPEN_LOOP_CHAR;
    g_motor_ctrl.state = MOTOR_STATE_STOPPED;
    g_motor_ctrl.direction = 1;
    g_motor_ctrl.target_speed_rpm = 600.0f;
    g_motor_ctrl.actual_speed_rpm = 0.0f;
    g_motor_ctrl.duty_pct = 15.0f;          /* 15% safe starting duty */
    g_motor_ctrl.electrical_freq_hz = 35.0f; /* 35 Hz = 300 RPM for 7 pole-pairs */
    g_motor_ctrl.pole_pairs = 7;
    g_motor_ctrl.current_limit_amps = 1.2f;
    g_motor_ctrl.run_time_ms = 0;

    IHM07M1_Init();
    IHM07M1_DisableBridge();
}

void Motor_Start(void) {
    if (g_motor_ctrl.state == MOTOR_STATE_FAULT) {
        Motor_ResetFault();
    }
    if (g_motor_ctrl.state == MOTOR_STATE_STOPPED) {
        g_motor_ctrl.state = MOTOR_STATE_RUNNING;
        IHM07M1_StartPWM();
        IHM07M1_SetRedLED(false);
    }
}

void Motor_Stop(void) {
    g_motor_ctrl.state = MOTOR_STATE_STOPPED;
    IHM07M1_DisableBridge();
    IHM07M1_StopPWM();
    g_motor_ctrl.actual_speed_rpm = 0.0f;
}

void Motor_Brake(void) {
    if (g_motor_ctrl.state == MOTOR_STATE_RUNNING) {
        g_motor_ctrl.state = MOTOR_STATE_BRAKING;
    }
}

void Motor_SetDuty(float duty_pct) {
    if (duty_pct < 0.0f) duty_pct = 0.0f;
    if (duty_pct > 80.0f) duty_pct = 80.0f; /* Clamp to safe 80% maximum */
    g_motor_ctrl.duty_pct = duty_pct;
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
    IHM07M1_ClearFault();
    IHM07M1_SetRedLED(false);
    if (g_motor_ctrl.state == MOTOR_STATE_FAULT) {
        g_motor_ctrl.state = MOTOR_STATE_STOPPED;
    }
}

/**
 * @brief Periodic 1 ms Motor Control & Telemetry Execution Loop
 */
void Motor_Update1ms(void) {
    g_telemetry.sample_count++;

    /* 1. Hardware Fault Check */
    if (IHM07M1_IsFaultActive()) {
        g_motor_ctrl.state = MOTOR_STATE_FAULT;
        g_telemetry.fault_active = true;
        IHM07M1_DisableBridge();
        IHM07M1_SetRedLED(true);
        return;
    }
    g_telemetry.fault_active = false;

    /* 2. Read Telemetry Sensors */
    /* Bus Voltage */
    uint16_t raw_vbus = ReadADCChannel(IHM07M1_ADC_VBUS_CH);
    float adc_v = ((float)raw_vbus / 4095.0f) * 3.3f;
    g_telemetry.vbus_v = adc_v * IHM07M1_VBUS_DIVIDER_RATIO;

    /* Potentiometer */
    uint16_t raw_pot = ReadADCChannel(IHM07M1_ADC_POT_CH);
    g_telemetry.pot_pct = ((float)raw_pot / 4095.0f) * 100.0f;

    /* Temperature (Linearized NTC approximation around 25C) */
    uint16_t raw_temp = ReadADCChannel(IHM07M1_ADC_TEMP_CH);
    float temp_v = ((float)raw_temp / 4095.0f) * 3.3f;
    g_telemetry.temp_c = 25.0f + (1.65f - temp_v) * 35.0f;
    if (g_telemetry.temp_c < 0.0f) g_telemetry.temp_c = 0.0f;

    /* Current Shunts */
    uint16_t raw_ia = ReadADCChannel(IHM07M1_ADC_CURR_A_CH);
    uint16_t raw_ib = ReadADCChannel(IHM07M1_ADC_CURR_B_CH);
    uint16_t raw_ic = ReadADCChannel(IHM07M1_ADC_CURR_C_CH);
    g_telemetry.current_a_amps = (((float)raw_ia / 4095.0f) * 3.3f) / IHM07M1_CURRENT_SENSE_GAIN;
    g_telemetry.current_b_amps = (((float)raw_ib / 4095.0f) * 3.3f) / IHM07M1_CURRENT_SENSE_GAIN;
    g_telemetry.current_c_amps = (((float)raw_ic / 4095.0f) * 3.3f) / IHM07M1_CURRENT_SENSE_GAIN;

    float p_curr = g_telemetry.current_a_amps;
    if (g_telemetry.current_b_amps > p_curr) p_curr = g_telemetry.current_b_amps;
    if (g_telemetry.current_c_amps > p_curr) p_curr = g_telemetry.current_c_amps;
    g_telemetry.current_peak_amps = p_curr;

    /* Hall Sensors */
    g_telemetry.hall_state = IHM07M1_ReadHallSensors();

    /* 3. Motor State Handling */
    if (g_motor_ctrl.state == MOTOR_STATE_STOPPED) {
        IHM07M1_DisableBridge();
        g_motor_ctrl.actual_speed_rpm = 0.0f;
        return;
    }

    if (g_motor_ctrl.state == MOTOR_STATE_BRAKING) {
        /* Active controlled deceleration */
        if (g_motor_ctrl.duty_pct > 1.0f) {
            g_motor_ctrl.duty_pct -= 0.2f; /* Smooth deceleration slope */
        } else {
            Motor_Stop();
            return;
        }
    }

    g_motor_ctrl.run_time_ms++;

    /* 4. Drive Strategy Execution */
    if (g_motor_ctrl.mode == MOTOR_MODE_OPEN_LOOP_CHAR) {
        /* Smooth rotating sinusoidal vector for bringing up unknown motors safely */
        float freq = g_motor_ctrl.electrical_freq_hz;
        float d_theta = (TWO_PI * freq) / 1000.0f;
        if (g_motor_ctrl.direction > 0) {
            g_open_loop_angle += d_theta;
        } else {
            g_open_loop_angle -= d_theta;
        }
        if (g_open_loop_angle >= TWO_PI) g_open_loop_angle -= TWO_PI;
        if (g_open_loop_angle < 0.0f) g_open_loop_angle += TWO_PI;

        float u_norm = 0.5f + 0.5f * sinf(g_open_loop_angle);
        float v_norm = 0.5f + 0.5f * sinf(g_open_loop_angle - (2.0f * PI_FLOAT / 3.0f));
        float w_norm = 0.5f + 0.5f * sinf(g_open_loop_angle + (2.0f * PI_FLOAT / 3.0f));

        float max_arr = (float)IHM07M1_PWM_ARR;
        float duty_scale = (g_motor_ctrl.duty_pct / 100.0f) * max_arr;

        uint16_t ccr_u = (uint16_t)(u_norm * duty_scale);
        uint16_t ccr_v = (uint16_t)(v_norm * duty_scale);
        uint16_t ccr_w = (uint16_t)(w_norm * duty_scale);

        /* Enable all three phases */
        IHM07M1_EnablePhase(0x07);
        IHM07M1_SetPWM(ccr_u, ccr_v, ccr_w);

        /* Estimated speed based on electrical frequency */
        g_motor_ctrl.actual_speed_rpm = (freq * 60.0f) / (float)g_motor_ctrl.pole_pairs;

    } else if (g_motor_ctrl.mode == MOTOR_MODE_SIX_STEP_VOLTAGE) {
        /* 6-step trapezoidal commutation using Hall sensor feedback (or simulated step) */
        uint8_t step = g_telemetry.hall_state;
        if (step < 1 || step > 6) {
            /* Fallback to angle-based step if Hall sensors unplugged */
            step = (uint8_t)((g_open_loop_angle / TWO_PI) * 6.0f) + 1;
            if (step > 6) step = 6;
        }

        uint16_t pwm_val = (uint16_t)((g_motor_ctrl.duty_pct / 100.0f) * (float)IHM07M1_PWM_ARR);

        /* 6-step commutation table:
           Step 1: U=PWM, V=GND, W=FLOAT (EN: U=1, V=1, W=0)
           Step 2: U=PWM, V=FLOAT, W=GND (EN: U=1, V=0, W=1)
           Step 3: U=FLOAT, V=PWM, W=GND (EN: U=0, V=1, W=1)
           Step 4: U=GND, V=PWM, W=FLOAT (EN: U=1, V=1, W=0)
           Step 5: U=GND, V=FLOAT, W=PWM (EN: U=1, V=0, W=1)
           Step 6: U=FLOAT, V=GND, W=PWM (EN: U=0, V=1, W=1)
        */
        switch (step) {
            case 1:
                IHM07M1_EnablePhase(0x03); /* U & V enabled, W floating */
                IHM07M1_SetPWM(pwm_val, 0, 0);
                break;
            case 2:
                IHM07M1_EnablePhase(0x05); /* U & W enabled, V floating */
                IHM07M1_SetPWM(pwm_val, 0, 0);
                break;
            case 3:
                IHM07M1_EnablePhase(0x06); /* V & W enabled, U floating */
                IHM07M1_SetPWM(0, pwm_val, 0);
                break;
            case 4:
                IHM07M1_EnablePhase(0x03); /* U & V enabled, W floating */
                IHM07M1_SetPWM(0, pwm_val, 0);
                break;
            case 5:
                IHM07M1_EnablePhase(0x05); /* U & W enabled, V floating */
                IHM07M1_SetPWM(0, 0, pwm_val);
                break;
            case 6:
                IHM07M1_EnablePhase(0x06); /* V & W enabled, U floating */
                IHM07M1_SetPWM(0, 0, pwm_val);
                break;
            default:
                IHM07M1_DisableBridge();
                break;
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
