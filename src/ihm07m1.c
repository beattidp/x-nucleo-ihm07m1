#include "ihm07m1.h"
#include "motor_control.h"
#include "stm32g4xx_ll_adc.h"

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

    /* 3. Configure L6230 DIAG/BKIN Fault Pin (PA6) - Open Drain with Pull-up */
    GPIO_InitStruct.Pin = IHM07M1_DIAG_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
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
    htim1.Init.RepetitionCounter = 1; /* Center-aligned mode: UEV fires once per PWM cycle (20 kHz) at underflow */
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

    /* Configure Master Trigger Output (TRGO) on Update Event for Injected ADC synchronization */
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig);

    /* Configure Hardware Break Input (BKIN) from L6230 DIAG with 1.5 us digital filter */
    TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
    sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
    sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
    sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
    sBreakDeadTimeConfig.DeadTime = 0;
    sBreakDeadTimeConfig.BreakState = TIM_BREAK_ENABLE;
    sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_LOW;
    sBreakDeadTimeConfig.BreakFilter = 15; /* Maximum noise filter: ~1.5 us */
    sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig);

    /* Configure TIM1 Update Interrupt (20 kHz PWM Commutation Update) */
    HAL_NVIC_SetPriority(TIM1_UP_TIM16_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);

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

    /* Set default safe hardware trip current limit: 2.2 Amps (below L6230 2.8A peak limit) */
    IHM07M1_SetCurrentReference(2.2f);

    /* 7. Configure Hall & Optical Tachometer Inputs */
    /* PA15 (Hall 1) */
    GPIO_InitStruct.Pin = IHM07M1_HALL1_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(IHM07M1_HALL1_PORT, &GPIO_InitStruct);

    /* PB3 (Hall 2) */
    GPIO_InitStruct.Pin = IHM07M1_HALL2_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(IHM07M1_HALL2_PORT, &GPIO_InitStruct);

    /* PB10 (Optical Tachometer / Hall 3 on J3-Z+): TIM2_CH3 Input Capture */
    GPIO_InitStruct.Pin = IHM07M1_HALL3_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = IHM07M1_HALL3_AF;
    HAL_GPIO_Init(IHM07M1_HALL3_PORT, &GPIO_InitStruct);

    /* Initialize TIM2 32-bit timebase for optical period measurement:
     * Clock = 170 MHz. Prescaler = 169 -> 1.000 MHz (1.0 us tick resolution).
     * Period = 0xFFFFFFFF (32-bit free-running, rollover = 4,294.9 seconds / 71.5 minutes) */
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 169;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFFU;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_IC_Init(&htim2);

    TIM_IC_InitTypeDef sICConfig = {0};
    sICConfig.ICPolarity = TIM_ICPOLARITY_FALLING; /* Falling edge active (default high) */
    sICConfig.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sICConfig.ICPrescaler = TIM_ICPSC_DIV1;
    sICConfig.ICFilter = 0x04; /* Glitch rejection filter against PWM transients */
    HAL_TIM_IC_ConfigChannel(&htim2, &sICConfig, TIM_CHANNEL_3);

    HAL_NVIC_SetPriority(TIM2_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_3);

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
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
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

    /* Enable ADC1 peripheral and wait until ADRDY becomes active */
    LL_ADC_ClearFlag_ADRDY(hadc1.Instance);
    LL_ADC_Enable(hadc1.Instance);
    while (!LL_ADC_IsActiveFlag_ADRDY(hadc1.Instance)) {
        LL_ADC_Enable(hadc1.Instance);
    }

    /* Pre-configure channel sampling times */
    LL_ADC_SetChannelSamplingTime(hadc1.Instance, IHM07M1_ADC_CURR_A_CH, LL_ADC_SAMPLINGTIME_6CYCLES_5);
    LL_ADC_SetChannelSamplingTime(hadc1.Instance, IHM07M1_ADC_CURR_B_CH, LL_ADC_SAMPLINGTIME_6CYCLES_5);
    LL_ADC_SetChannelSamplingTime(hadc1.Instance, IHM07M1_ADC_CURR_C_CH, LL_ADC_SAMPLINGTIME_6CYCLES_5);
    LL_ADC_SetChannelSamplingTime(hadc1.Instance, IHM07M1_ADC_VBUS_CH, LL_ADC_SAMPLINGTIME_47CYCLES_5);
    LL_ADC_SetChannelSamplingTime(hadc1.Instance, IHM07M1_ADC_TEMP_CH, LL_ADC_SAMPLINGTIME_47CYCLES_5);
    LL_ADC_SetChannelSamplingTime(hadc1.Instance, IHM07M1_ADC_POT_CH, LL_ADC_SAMPLINGTIME_47CYCLES_5);

    /* Configure ADC1 Injected Group for 3-Shunt Current Sampling (TIM1 TRGO Triggered) */
    ADC_InjectionConfTypeDef sConfigInjected = {0};
    sConfigInjected.InjectedNbrOfConversion = 3;
    sConfigInjected.InjectedDiscontinuousConvMode = DISABLE;
    sConfigInjected.AutoInjectedConv = DISABLE;
    sConfigInjected.QueueInjectedContext = DISABLE;
    sConfigInjected.ExternalTrigInjecConv = ADC_EXTERNALTRIGINJEC_T1_TRGO;
    sConfigInjected.ExternalTrigInjecConvEdge = ADC_EXTERNALTRIGINJECCONV_EDGE_RISING;
    sConfigInjected.InjecOversamplingMode = DISABLE;
    sConfigInjected.InjectedSingleDiff = ADC_SINGLE_ENDED;
    sConfigInjected.InjectedOffsetNumber = ADC_OFFSET_NONE;
    sConfigInjected.InjectedOffset = 0;

    /* Rank 1: Phase A (PA0 / ADC1_IN1) */
    sConfigInjected.InjectedChannel = IHM07M1_ADC_CURR_A_CH;
    sConfigInjected.InjectedRank = ADC_INJECTED_RANK_1;
    sConfigInjected.InjectedSamplingTime = ADC_SAMPLETIME_6CYCLES_5;
    HAL_ADCEx_InjectedConfigChannel(&hadc1, &sConfigInjected);

    /* Rank 2: Phase B (PC1 / ADC1_IN7) */
    sConfigInjected.InjectedChannel = IHM07M1_ADC_CURR_B_CH;
    sConfigInjected.InjectedRank = ADC_INJECTED_RANK_2;
    sConfigInjected.InjectedSamplingTime = ADC_SAMPLETIME_6CYCLES_5;
    HAL_ADCEx_InjectedConfigChannel(&hadc1, &sConfigInjected);

    /* Rank 3: Phase C (PC0 / ADC1_IN6) */
    sConfigInjected.InjectedChannel = IHM07M1_ADC_CURR_C_CH;
    sConfigInjected.InjectedRank = ADC_INJECTED_RANK_3;
    sConfigInjected.InjectedSamplingTime = ADC_SAMPLETIME_6CYCLES_5;
    HAL_ADCEx_InjectedConfigChannel(&hadc1, &sConfigInjected);

    /* Arm Injected ADC conversions triggered on TIM1 TRGO */
    IHM07M1_StartInjectedSampling();
}

/**
 * @brief Set L6230 current reference threshold in Amperes (via TIM16_CH1 PWM)
 *        Calibrated to match ST X-CUBE-SPN7 hardware reference:
 *        2.2 A peak = 488 counts (48.8% duty, ~1.61V on PB4).
 */
void IHM07M1_SetCurrentReference(float max_amps) {
    if (max_amps < 0.3f) max_amps = 0.3f;
    if (max_amps > 2.6f) max_amps = 2.6f;

    /* pulse = max_amps * (2000 / 4096 / 2.2) * ARR = max_amps * 221.7f */
    uint32_t pulse = (uint32_t)(max_amps * 221.7f);
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
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    IHM07M1_StartInjectedSampling();
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_ENABLE(&htim1);
    __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
}

/**
 * @brief Stop TIM1 PWM generation
 */
void IHM07M1_StopPWM(void) {
    __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_DISABLE(&htim1);
}

/**
 * @brief TIM1 Update Interrupt Handler (20 kHz PWM cycle update)
 */
void TIM1_UP_TIM16_IRQHandler(void) {
    if (__HAL_TIM_GET_FLAG(&htim1, TIM_FLAG_UPDATE) != RESET) {
        __HAL_TIM_CLEAR_IT(&htim1, TIM_IT_UPDATE);
        Motor_PWM_UpdateISR();
    }
    if (__HAL_TIM_GET_FLAG(&htim16, TIM_FLAG_UPDATE) != RESET) {
        __HAL_TIM_CLEAR_IT(&htim16, TIM_IT_UPDATE);
    }
    /* Read back SR to ensure write buffer drain before ISR exit */
    (void)htim1.Instance->SR;
}

/**
 * @brief TIM2 Interrupt Handler (Optical photodetector capture on PB10 / TIM2_CH3)
 */
void TIM2_IRQHandler(void) {
    if (__HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_CC3) != RESET) {
        __HAL_TIM_CLEAR_IT(&htim2, TIM_IT_CC3);
        uint32_t capture = htim2.Instance->CCR3;
        Motor_OpticalCaptureISR(capture);
    }
    (void)htim2.Instance->SR;
}

/**
 * @brief Check if L6230 DIAG pin indicates overcurrent or thermal shutdown
 */
bool IHM07M1_IsFaultActive(void) {
    /* Check hardware break latched in TIM1 (filtered by BreakFilter) */
    if (__HAL_TIM_GET_FLAG(&htim1, TIM_FLAG_BREAK) != RESET ||
        __HAL_TIM_GET_FLAG(&htim1, TIM_FLAG_BREAK2) != RESET) {
        return true;
    }
    /* Pin is open-drain active LOW. Debounce against transient noise spikes */
    if (HAL_GPIO_ReadPin(IHM07M1_DIAG_PORT, IHM07M1_DIAG_PIN) == GPIO_PIN_RESET) {
        for (volatile int i = 0; i < 40; i++);
        if (HAL_GPIO_ReadPin(IHM07M1_DIAG_PORT, IHM07M1_DIAG_PIN) == GPIO_PIN_RESET) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Clear hardware break and fault flags
 */
void IHM07M1_ClearFault(void) {
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK);
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK2);
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

/**
 * @brief Read an ADC channel synchronously using LL macros without HAL_GetTick dependency
 */
static uint16_t s_injected_offset_a = 2048;
static uint16_t s_injected_offset_b = 2048;
static uint16_t s_injected_offset_c = 2048;

/**
 * @brief Read an ADC channel synchronously using LL macros without HAL_GetTick dependency
 */
static uint16_t IHM07M1_ReadADCChannel(uint32_t channel) {
    if (!LL_ADC_IsEnabled(hadc1.Instance)) {
        return 0;
    }

    LL_ADC_REG_SetSequencerRanks(hadc1.Instance, LL_ADC_REG_RANK_1, channel);

    LL_ADC_ClearFlag_EOC(hadc1.Instance);
    LL_ADC_ClearFlag_EOS(hadc1.Instance);

    LL_ADC_REG_StartConversion(hadc1.Instance);

    uint32_t timeout = 5000;
    while (!LL_ADC_IsActiveFlag_EOC(hadc1.Instance)) {
        if (--timeout == 0) {
            LL_ADC_REG_StopConversion(hadc1.Instance);
            return 0;
        }
    }
    return (uint16_t)LL_ADC_REG_ReadConversionData12(hadc1.Instance);
}

float IHM07M1_ReadVBusVoltage(void) {
    uint16_t raw = IHM07M1_ReadADCChannel(IHM07M1_ADC_VBUS_CH);
    float adc_v = ((float)raw / 4095.0f) * 3.3f;
    return adc_v * IHM07M1_VBUS_DIVIDER_RATIO;
}

float IHM07M1_ReadTemperature(void) {
    uint16_t raw = IHM07M1_ReadADCChannel(IHM07M1_ADC_TEMP_CH);
    float temp_v = ((float)raw / 4095.0f) * 3.3f;
    float temp_c = 25.0f + (1.65f - temp_v) * 35.0f;
    if (temp_c < 0.0f) temp_c = 0.0f;
    return temp_c;
}

float IHM07M1_ReadPotentiometer(void) {
    uint16_t raw = IHM07M1_ReadADCChannel(IHM07M1_ADC_POT_CH);
    return ((float)raw / 4095.0f) * 100.0f;
}

void IHM07M1_ReadRawCurrentSensors(uint16_t* raw_a, uint16_t* raw_b, uint16_t* raw_c) {
    if (raw_a) *raw_a = IHM07M1_ReadADCChannel(IHM07M1_ADC_CURR_A_CH);
    if (raw_b) *raw_b = IHM07M1_ReadADCChannel(IHM07M1_ADC_CURR_B_CH);
    if (raw_c) *raw_c = IHM07M1_ReadADCChannel(IHM07M1_ADC_CURR_C_CH);
}

void IHM07M1_CalibrateCurrentOffsets(uint16_t* offset_a, uint16_t* offset_b, uint16_t* offset_c) {
    IHM07M1_DisableBridge();
    for (volatile int i = 0; i < 2000; i++);

    uint32_t sum_a = 0;
    uint32_t sum_b = 0;
    uint32_t sum_c = 0;
    const uint16_t num_samples = 64;

    for (uint16_t i = 0; i < num_samples; i++) {
        sum_a += IHM07M1_ReadADCChannel(IHM07M1_ADC_CURR_A_CH);
        sum_b += IHM07M1_ReadADCChannel(IHM07M1_ADC_CURR_B_CH);
        sum_c += IHM07M1_ReadADCChannel(IHM07M1_ADC_CURR_C_CH);
        for (volatile int wait = 0; wait < 100; wait++);
    }

    s_injected_offset_a = (uint16_t)(sum_a / num_samples);
    s_injected_offset_b = (uint16_t)(sum_b / num_samples);
    s_injected_offset_c = (uint16_t)(sum_c / num_samples);

    if (offset_a) *offset_a = s_injected_offset_a;
    if (offset_b) *offset_b = s_injected_offset_b;
    if (offset_c) *offset_c = s_injected_offset_c;
}

/**
 * @brief Arm / Start continuous hardware-triggered ADC1 injected conversions
 */
void IHM07M1_StartInjectedSampling(void) {
    LL_ADC_ClearFlag_JEOC(hadc1.Instance);
    LL_ADC_ClearFlag_JEOS(hadc1.Instance);
    HAL_ADCEx_InjectedStart(&hadc1);
}

/**
 * @brief Retrieve synchronized 3-shunt currents sampled at PWM valley
 * @param ia Phase A current in Amperes
 * @param ib Phase B current in Amperes
 * @param ic Phase C current in Amperes
 */
void IHM07M1_GetInjectedCurrents(float* ia, float* ib, float* ic) {
    /* Poll JEOS flag with short timeout (~400 loop spins = ~2.5 us) */
    uint32_t timeout = 400;
    while (!LL_ADC_IsActiveFlag_JEOS(hadc1.Instance)) {
        if (--timeout == 0) break;
    }
    if (LL_ADC_IsActiveFlag_JEOS(hadc1.Instance)) {
        LL_ADC_ClearFlag_JEOS(hadc1.Instance);
    }

    uint16_t raw_a = (uint16_t)hadc1.Instance->JDR1;
    uint16_t raw_b = (uint16_t)hadc1.Instance->JDR2;
    uint16_t raw_c = (uint16_t)hadc1.Instance->JDR3;

    int32_t diff_a = (int32_t)raw_a - (int32_t)s_injected_offset_a;
    int32_t diff_b = (int32_t)raw_b - (int32_t)s_injected_offset_b;
    int32_t diff_c = (int32_t)raw_c - (int32_t)s_injected_offset_c;

    const float lsb_to_amps = (3.3f / 4095.0f) / IHM07M1_CURRENT_SENSE_GAIN;

    if (ia) *ia = (float)diff_a * lsb_to_amps;
    if (ib) *ib = (float)diff_b * lsb_to_amps;
    if (ic) *ic = (float)diff_c * lsb_to_amps;
}

static const PowerStageCapabilities_t s_ihm07m1_caps = {
    .board_type = BOARD_TYPE_IHM07M1_L6230,
    .board_name = "X-NUCLEO-IHM07M1",
    .driver_ic = "L6230 DMOS 3-Phase Bridge",
    .max_voltage_v = 48.0f,
    .nominal_voltage_v = 24.0f,
    .max_continuous_current_a = 1.40f,
    .peak_current_a = 2.80f,
    .shunt_count = 3,
    .shunt_resistance_ohm = 0.33f,
    .opamp_gain = 1.53f,
    .current_sense_gain = IHM07M1_CURRENT_SENSE_GAIN,
    .vbus_divider_ratio = IHM07M1_VBUS_DIVIDER_RATIO,
    .pwm_arr = IHM07M1_PWM_ARR
};

const PowerStageCapabilities_t* IHM07M1_GetCapabilities(void) {
    return &s_ihm07m1_caps;
}

#if (TARGET_POWER_STAGE == BOARD_TYPE_IHM07M1_L6230)
/* Route unified PowerStage_* calls to IHM07M1 implementation */
const PowerStageCapabilities_t* PowerStage_GetCapabilities(void) { return IHM07M1_GetCapabilities(); }
void PowerStage_Init(void) { IHM07M1_Init(); }
void PowerStage_SetPWM(uint16_t u, uint16_t v, uint16_t w) { IHM07M1_SetPWM(u, v, w); }
void PowerStage_StartPWM(void) { IHM07M1_StartPWM(); }
void PowerStage_StopPWM(void) { IHM07M1_StopPWM(); }
void PowerStage_EnablePhase(uint8_t m) { IHM07M1_EnablePhase(m); }
void PowerStage_DisableBridge(void) { IHM07M1_DisableBridge(); }
bool PowerStage_IsFaultActive(void) { return IHM07M1_IsFaultActive(); }
void PowerStage_ClearFault(void) { IHM07M1_ClearFault(); }
void PowerStage_SetCurrentTripThreshold(float a) { IHM07M1_SetCurrentReference(a); }
uint8_t PowerStage_ReadHallSensors(void) { return IHM07M1_ReadHallSensors(); }
void PowerStage_SetStatusLED(bool s) { IHM07M1_SetRedLED(s); }
void PowerStage_ToggleStatusLED(void) { IHM07M1_ToggleRedLED(); }
float PowerStage_ReadVBusVoltage(void) { return IHM07M1_ReadVBusVoltage(); }
float PowerStage_ReadTemperature(void) { return IHM07M1_ReadTemperature(); }
float PowerStage_ReadPotentiometer(void) { return IHM07M1_ReadPotentiometer(); }
void PowerStage_ReadRawCurrentSensors(uint16_t* a, uint16_t* b, uint16_t* c) { IHM07M1_ReadRawCurrentSensors(a, b, c); }
void PowerStage_CalibrateCurrentOffsets(uint16_t* a, uint16_t* b, uint16_t* c) { IHM07M1_CalibrateCurrentOffsets(a, b, c); }
void PowerStage_GetInjectedCurrents(float* a, float* b, float* c) { IHM07M1_GetInjectedCurrents(a, b, c); }
#endif
