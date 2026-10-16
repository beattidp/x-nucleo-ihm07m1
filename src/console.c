#include "console.h"
#include "motor_control.h"
#include "ihm07m1.h"

UART_HandleTypeDef hlpuart1;

void console_init(void) {
    /* Enable GPIO and LPUART1 Clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_LPUART1_CLK_ENABLE();

    /* Configure PA2 (TX) and PA3 (RX) as LPUART1 Alternate Function */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = CONSOLE_TX_PIN | CONSOLE_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = CONSOLE_TX_AF;
    HAL_GPIO_Init(CONSOLE_TX_PORT, &GPIO_InitStruct);

    /* Initialize LPUART1 Peripheral @ 115200 baud, 8N1 */
    hlpuart1.Instance = LPUART1;
    hlpuart1.Init.BaudRate = 115200;
    hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
    hlpuart1.Init.StopBits = UART_STOPBITS_1;
    hlpuart1.Init.Parity = UART_PARITY_NONE;
    hlpuart1.Init.Mode = UART_MODE_TX_RX;
    hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    /* Select SYSCLK (170 MHz) as LPUART1 clock source */
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPUART1;
    PeriphClkInit.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_SYSCLK;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

    HAL_UART_Init(&hlpuart1);

    /* Route tinyprintf output to LPUART1 */
    init_printf(NULL, __io_putc);
}

void __io_putc(void* p, char c) {
    (void)p;
    HAL_UART_Transmit(&hlpuart1, (uint8_t*)&c, 1, 100);
}

bool console_has_char(void) {
    return (__HAL_UART_GET_FLAG(&hlpuart1, UART_FLAG_RXNE) != RESET);
}

int console_getch(void) {
    if (console_has_char()) {
        uint8_t ch = 0;
        if (HAL_UART_Receive(&hlpuart1, &ch, 1, 0) == HAL_OK) {
            return (int)ch;
        }
    }
    return -1;
}

/**
 * @brief Render fixed-position, non-scrolling ANSI terminal dashboard
 */
void Console_RenderDashboard(const MotorTelemetry_t* telem, const MotorControl_t* ctrl, char heartbeat) {
    uint32_t sec = HAL_GetTick() / 1000;
    uint32_t hours = sec / 3600;
    uint32_t minutes = (sec % 3600) / 60;
    uint32_t seconds = sec % 60;

    /* Reposition cursor at Row 1, Column 1 */
    printf("\033[H");
    printf("================================================================================\033[K\r\n");
    printf("      STM32G474RE + X-NUCLEO-IHM07M1 -- BLDC MOTOR EVALUATION DASHBOARD        \033[K\r\n");
    printf("================================================================================\033[K\r\n");
    printf(" Port   : LPUART1 (PA2/PA3) @ 115200 8N1   | CPU Clock: 170 MHz | PWM: 20 kHz\033[K\r\n");
    printf(" Uptime : %02u:%02u:%02u                         | Sample: #%05u  [Heartbeat: %c]\033[K\r\n",
           (unsigned int)hours, (unsigned int)minutes, (unsigned int)seconds, 
           (unsigned int)telem->sample_count, heartbeat);
    printf("--------------------------------------------------------------------------------\033[K\r\n");
    printf(" POWER STAGE TELEMETRY (X-NUCLEO-IHM07M1)\033[K\r\n");
    printf("--------------------------------------------------------------------------------\033[K\r\n");

    /* Bus Voltage */
    int vbus_int = (int)telem->vbus_v;
    int vbus_dec = (int)((telem->vbus_v - (float)vbus_int) * 100.0f);
    if (vbus_dec < 0) vbus_dec = -vbus_dec;
    const char* vbus_status = (telem->vbus_v < 8.0f) ? "[LOW/DISCONNECTED]" : 
                              (telem->vbus_v > 48.0f) ? "[OVERVOLTAGE]" : "[OK]";
    printf(" Bus Voltage (VBUS)         : %2d.%02d V %s\033[K\r\n", vbus_int, vbus_dec, vbus_status);

    /* Driver Temperature */
    int temp_int = (int)telem->temp_c;
    int temp_dec = (int)((telem->temp_c - (float)temp_int) * 10.0f);
    if (temp_dec < 0) temp_dec = -temp_dec;
    printf(" Driver Board Temperature   : %2d.%01d deg C\033[K\r\n", temp_int, temp_dec);

    /* Speed Potentiometer */
    int pot_int = (int)telem->pot_pct;
    int pot_dec = (int)((telem->pot_pct - (float)pot_int) * 10.0f);
    if (pot_dec < 0) pot_dec = -pot_dec;
    printf(" Speed Potentiometer (R42)  : %3d.%01d %%\033[K\r\n", pot_int, pot_dec);

    /* Driver Status & Fault */
    printf(" Driver Status (L6230 DIAG) : ");
    if (telem->fault_active) {
        printf("\033[1;31m[FAULT / OVERCURRENT TRIPPED]\033[0m\033[K\r\n");
    } else {
        printf("\033[1;32m[READY / NORMAL]\033[0m\033[K\r\n");
    }

    /* Hall State */
    printf(" Hall Sensor Signals (H321) : State %d [H1=%d, H2=%d, H3=%d]\033[K\r\n",
           telem->hall_state, (telem->hall_state & 1), ((telem->hall_state >> 1) & 1), ((telem->hall_state >> 2) & 1));

    printf("--------------------------------------------------------------------------------\033[K\r\n");
    printf(" MOTOR DRIVE STATUS\033[K\r\n");
    printf("--------------------------------------------------------------------------------\033[K\r\n");

    /* State and Mode */
    const char* state_str = (ctrl->state == MOTOR_STATE_STOPPED) ? "STOPPED" :
                            (ctrl->state == MOTOR_STATE_STARTING) ? "STARTING" :
                            (ctrl->state == MOTOR_STATE_RUNNING) ? "RUNNING" :
                            (ctrl->state == MOTOR_STATE_BRAKING) ? "ACTIVE BRAKING" : "FAULT";

    const char* mode_str = (ctrl->mode == MOTOR_MODE_OPEN_LOOP_CHAR) ? "STAGE 1: OPEN-LOOP V/F CHAR" :
                           (ctrl->mode == MOTOR_MODE_SIX_STEP_VOLTAGE) ? "STAGE 2: 6-STEP VOLTAGE MODE" : "STAGE 3: 4-QUADRANT FOC";

    printf(" Drive State                : %s\033[K\r\n", state_str);
    printf(" Control Strategy           : %s\033[K\r\n", mode_str);

    /* Target & Actual Speed */
    int spd_int = (int)ctrl->actual_speed_rpm;
    int tgt_int = (int)ctrl->target_speed_rpm;
    int duty_int = (int)ctrl->duty_pct;
    int duty_dec = (int)((ctrl->duty_pct - (float)duty_int) * 10.0f);
    if (duty_dec < 0) duty_dec = -duty_dec;

    printf(" Target / Estimated Speed   : Target %4d RPM | Est %4d RPM (%s)\033[K\r\n",
           tgt_int, spd_int, (ctrl->direction > 0) ? "CW" : "CCW");
    printf(" Commanded Duty Cycle       : %3d.%01d %% (ARR = %u)\033[K\r\n", duty_int, duty_dec, IHM07M1_PWM_ARR);

    /* Phase Currents */
    int ia_ma = (int)(telem->current_a_amps * 1000.0f);
    int ib_ma = (int)(telem->current_b_amps * 1000.0f);
    int ic_ma = (int)(telem->current_c_amps * 1000.0f);
    int ipk_ma = (int)(telem->current_peak_amps * 1000.0f);
    printf(" Phase Currents (mA)        : IA=%4d mA | IB=%4d mA | IC=%4d mA [Pk=%4d mA]\033[K\r\n",
           ia_ma, ib_ma, ic_ma, ipk_ma);

    printf(" Current Trip Limit         : %.2f A (PWM Ref on PB4)\033[K\r\n", ctrl->current_limit_amps);

    printf("================================================================================\033[K\r\n");
    printf(" INTERACTIVE COMMANDS:                                                          \033[K\r\n");
    printf("  [SPACE] Start/Stop Motor   |  [+/-] Inc/Dec Duty Cycle  |  [D] Toggle Direction\033[K\r\n");
    printf("  [M] Toggle Control Mode    |  [B] Active Brake          |  [R] Reset Fault     \033[K\r\n");
    printf("================================================================================\033[K\r\n");
}
