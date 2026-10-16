#include "ihm07m1.h"

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim16;
TIM_HandleTypeDef htim2;
ADC_HandleTypeDef hadc1;

/**
 * @brief Initialize all GPIOs, Timers, and ADCs for the X-NUCLEO-IHM07M1 expansion board
 */
void IHM07M1_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable GPIO Clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM16_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_ADC12_CLK_ENABLE();

    /* 1. Configure Half-Bridge Enable Pins (PC10, PC11, PC12) - Active HIGH */
    HAL_GPIO_WritePin(IHM07M1_EN1_PORT, IHM07M1_EN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(IHM07M1_EN2_PORT, IHM07M1_EN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(IHM07M1_EN3_PORT, IHM07M1_EN3_PIN, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = IHM07M1_EN1_PIN | IHM07M1_EN2_PIN | IHM07M1_EN3_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* 2. Configure Red Fault/User LED (PB2) */
    HAL_GPIO_WritePin(IHM07M1_RED_LED_PORT, IHM07M1_RED_LED_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = IHM07M1_RED_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(IHM07M1_RED_LED_PORT, &GPIO_InitStruct);

    /* 3. Configure L6230 DIAG/BKIN Fault Pin (PA6) */
    GPIO_InitStruct.Pin = IHM07M1_DIAG_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = IHM07M1_DIAG_AF;
    HAL_GPIO_Init(IHM07M1_DIAG_PORT, &GPIO_InitStruct);

    /* 4. Configure TIM1 High-Side PWM Pins (PA8, PA9, PA10) */
    GPIO_InitStruct.Pin = IHM07M1_IN1_PIN | IHM07M1_IN2_PIN | IHM07M1_IN3_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = IHM07M1_IN1_AF;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 5. Initialize TIM1 Timebase (20 kHz center-aligned PWM) */
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
    htim1.Init.Period = IHM07M1_PWM_ARR;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;

    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3);

    /* Configure Hardware Break Input (BKIN) from L6230 DIAG */
    TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
    sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
    sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
    sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
    sBreakDeadTimeConfig.DeadTime = 0;
    sBreakDeadTimeConfig.BreakState = TIM_BREAK_ENABLE;
    sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_LOW;
    sBreakDeadTimeConfig.BreakFilter = 2;
    sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig);

    /* 6. Configure Current Reference Generation (TIM16_CH1 on PB4) */
    GPIO_InitStruct.Pin = IHM07M1_REF_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = IHM07M1_REF_AF;
    HAL_GPIO_Init(IHM07M1_REF_PORT, &GPIO_InitStruct);

    htim16.Instance = TIM16;
    htim16.Init.Prescaler = 0;
    htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim16.Init.Period = IHM07M1_REF_ARR;
    htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim16.Init.RepetitionCounter = 0;
    htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim16);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    HAL_TIM_PWM_ConfigChannel(&htim16, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim16, TIM_CHANNEL_1);

    /* Set default safe current limit: 1.2 Amps */
    IHM07M1_SetCurrentReference(1.2f);

    /* 7. Configure Hall Sensor Inputs (PA15, PB3, PB10) */
    GPIO_InitStruct.Pin = IHM07M1_HALL1_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(IHM07M1_HALL1_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = IHM07M1_HALL2_PIN | IHM07M1_HALL3_PIN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* 8. Configure ADC1 for Telemetry (VBUS, Temp, Potentiometer, Currents) */
    GPIO_InitStruct.Pin = IHM07M1_ADC_CURR_A_PIN | IHM07M1_ADC_VBUS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = IHM07M1_ADC_POT_PIN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = IHM07M1_ADC_TEMP_PIN | IHM07M1_ADC_CURR_B_PIN | IHM07M1_ADC_CURR_C_PIN;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC12;
    PeriphClkInit.Adc12ClockSelection = RCC_ADC12CLKSOURCE_SYSCLK;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode = DISABLE;
    HAL_ADC_Init(&hadc1);

    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
}

/**
 * @brief Set L6230 current reference threshold in Amperes (via TIM16_CH1 PWM)
 */
void IHM07M1_SetCurrentReference(float max_amps) {
    if (max_amps < 0.2f) max_amps = 0.2f;
    if (max_amps > 2.5f) max_amps = 2.5f;

    /* V_ref = max_amps * 0.33 Ohm */
    /* Duty = V_ref / 3.3V = max_amps * 0.10 */
    /* Pulse = Duty * ARR (ARR = 999) = max_amps * 99.9 */
    uint32_t pulse = (uint32_t)(max_amps * 99.9f);
    if (pulse > IHM07M1_REF_ARR) pulse = IHM07M1_REF_ARR;

    __HAL_TIM_SET_COMPARE(&htim16, TIM_CHANNEL_1, pulse);
}

/**
 * @brief Enable individual bridge phases via EN1, EN2, EN3
 * @param phase_mask Bit0 = Phase U (PC10), Bit1 = Phase V (PC11), Bit2 = Phase W (PC12)
 */
void IHM07M1_EnablePhase(uint8_t phase_mask) {
    HAL_GPIO_WritePin(IHM07M1_EN1_PORT, IHM07M1_EN1_PIN, (phase_mask & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(IHM07M1_EN2_PORT, IHM07M1_EN2_PIN, (phase_mask & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(IHM07M1_EN3_PORT, IHM07M1_EN3_PIN, (phase_mask & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief Disable all half bridges (all outputs float / Hi-Z)
 */
void IHM07M1_DisableBridge(void) {
    IHM07M1_EnablePhase(0x00);
    IHM07M1_SetPWM(0, 0, 0);
}

/**
 * @brief Set compare duty registers for U, V, W phases
 */
void IHM07M1_SetPWM(uint16_t duty_u, uint16_t duty_v, uint16_t duty_w) {
    if (duty_u > IHM07M1_PWM_MAX_DUTY) duty_u = IHM07M1_PWM_MAX_DUTY;
    if (duty_v > IHM07M1_PWM_MAX_DUTY) duty_v = IHM07M1_PWM_MAX_DUTY;
    if (duty_w > IHM07M1_PWM_MAX_DUTY) duty_w = IHM07M1_PWM_MAX_DUTY;

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty_u);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, duty_v);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, duty_w);
}

/**
 * @brief Start TIM1 PWM generation
 */
void IHM07M1_StartPWM(void) {
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_ENABLE(&htim1);
}

/**
 * @brief Stop TIM1 PWM generation
 */
void IHM07M1_StopPWM(void) {
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_DISABLE(&htim1);
}

/**
 * @brief Check if L6230 DIAG pin indicates overcurrent or thermal shutdown
 */
bool IHM07M1_IsFaultActive(void) {
    /* Pin is open-drain active LOW */
    if (HAL_GPIO_ReadPin(IHM07M1_DIAG_PORT, IHM07M1_DIAG_PIN) == GPIO_PIN_RESET) {
        return true;
    }
    if (__HAL_TIM_GET_FLAG(&htim1, TIM_FLAG_BREAK) != RESET) {
        return true;
    }
    return false;
}

/**
 * @brief Clear hardware break and fault flags
 */
void IHM07M1_ClearFault(void) {
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK);
    __HAL_TIM_MOE_ENABLE(&htim1);
}

/**
 * @brief Control onboard Red LED D11
 */
void IHM07M1_SetRedLED(bool state) {
    HAL_GPIO_WritePin(IHM07M1_RED_LED_PORT, IHM07M1_RED_LED_PIN, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void IHM07M1_ToggleRedLED(void) {
    HAL_GPIO_TogglePin(IHM07M1_RED_LED_PORT, IHM07M1_RED_LED_PIN);
}

/**
 * @brief Read Hall sensors state (1 to 6)
 */
uint8_t IHM07M1_ReadHallSensors(void) {
    uint8_t h1 = (HAL_GPIO_ReadPin(IHM07M1_HALL1_PORT, IHM07M1_HALL1_PIN) == GPIO_PIN_SET) ? 1 : 0;
    uint8_t h2 = (HAL_GPIO_ReadPin(IHM07M1_HALL2_PORT, IHM07M1_HALL2_PIN) == GPIO_PIN_SET) ? 1 : 0;
    uint8_t h3 = (HAL_GPIO_ReadPin(IHM07M1_HALL3_PORT, IHM07M1_HALL3_PIN) == GPIO_PIN_SET) ? 1 : 0;
    return (uint8_t)((h3 << 2) | (h2 << 1) | h1);
}
