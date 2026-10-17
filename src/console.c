#include "console.h"
#include "motor_control.h"
#include "motor_profiles.h"
#include "motor_characterization.h"
#include "power_stage.h"
#include <string.h>

UART_HandleTypeDef hlpuart1;

/* TX Ring Buffer */
static uint8_t s_tx_buf[CONSOLE_TX_BUF_SIZE];
static volatile uint16_t s_tx_head = 0;
static volatile uint16_t s_tx_tail = 0;

/* RX Ring Buffer */
static uint8_t s_rx_buf[CONSOLE_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;

static volatile bool s_console_ready = false;

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

    /* Configure LPUART1 NVIC Interrupt (Priority 5, below SysTick) */
    HAL_NVIC_SetPriority(LPUART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(LPUART1_IRQn);

    /* Enable RX Interrupt */
    __HAL_UART_ENABLE_IT(&hlpuart1, UART_IT_RXNE);

    s_console_ready = true;
}

/**
 * @brief Enqueue data into TX circular buffer and start interrupt transmission
 */
int console_write(const uint8_t *data, uint16_t len) {
    if (!s_console_ready) return 0;

    for (uint16_t i = 0; i < len; i++) {
        uint16_t next_head = (s_tx_head + 1) & (CONSOLE_TX_BUF_SIZE - 1);
        /* If buffer full, enable TX interrupt and spin with timeout */
        uint32_t spin = 20000;
        while (next_head == s_tx_tail) {
            __HAL_UART_ENABLE_IT(&hlpuart1, UART_IT_TXE);
            if (--spin == 0) break;
        }
        if (next_head == s_tx_tail) {
            /* Saturated: drop remaining characters to preserve existing buffer */
            break;
        }
        s_tx_buf[s_tx_head] = data[i];
        s_tx_head = next_head;
    }
    __HAL_UART_ENABLE_IT(&hlpuart1, UART_IT_TXE);
    return (int)len;
}

int console_putc(char c) {
    return console_write((const uint8_t*)&c, 1);
}

bool console_has_char(void) {
    return (s_rx_head != s_rx_tail);
}

int console_getch(void) {
    if (s_rx_head != s_rx_tail) {
        int ch = s_rx_buf[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1) & (CONSOLE_RX_BUF_SIZE - 1);
        return ch;
    }
    return -1;
}

uint16_t console_tx_queue_len(void) {
    return (s_tx_head - s_tx_tail) & (CONSOLE_TX_BUF_SIZE - 1);
}

/**
 * @brief Standard library _write hook for printf
 */
int _write(int file, char *ptr, int len) {
    (void)file;
    return console_write((const uint8_t*)ptr, (uint16_t)len);
}

/**
 * @brief Standard library _read hook for scanf / getchar
 */
int _read(int file, char *ptr, int len) {
    (void)file;
    int count = 0;
    while (count < len) {
        int ch = console_getch();
        if (ch < 0) break;
        ptr[count++] = (char)ch;
    }
    return count;
}

/**
 * @brief LPUART1 Interrupt Service Routine
 */
void LPUART1_IRQHandler(void) {
    uint32_t isrflags = hlpuart1.Instance->ISR;
    uint32_t cr1 = hlpuart1.Instance->CR1;

    /* Handle TX Data Register Empty */
    if ((isrflags & USART_ISR_TXE_TXFNF) && (cr1 & USART_CR1_TXEIE_TXFNFIE)) {
        if (s_tx_head != s_tx_tail) {
            hlpuart1.Instance->TDR = s_tx_buf[s_tx_tail];
            s_tx_tail = (s_tx_tail + 1) & (CONSOLE_TX_BUF_SIZE - 1);
        } else {
            /* Queue empty, disable TXE interrupt */
            __HAL_UART_DISABLE_IT(&hlpuart1, UART_IT_TXE);
        }
    }

    /* Handle RX Data Register Not Empty */
    if ((isrflags & USART_ISR_RXNE_RXFNE) && (cr1 & USART_CR1_RXNEIE_RXFNEIE)) {
        uint8_t ch = (uint8_t)(hlpuart1.Instance->RDR & 0xFF);
        uint16_t next_head = (s_rx_head + 1) & (CONSOLE_RX_BUF_SIZE - 1);
        if (next_head != s_rx_tail) {
            s_rx_buf[s_rx_head] = ch;
            s_rx_head = next_head;
        }
    }

    /* Clear Overrun, Noise, Framing, Parity Errors */
    if (isrflags & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE | USART_ISR_PE)) {
        hlpuart1.Instance->ICR = (USART_ICR_ORECF | USART_ICR_NECF | USART_ICR_FECF | USART_ICR_PECF);
    }
}

/**
 * @brief Render fixed-position, non-scrolling ANSI terminal dashboard
 */
void Console_RenderDashboard(const MotorTelemetry_t* telem, const MotorControl_t* ctrl, char heartbeat) {
    uint32_t sec = HAL_GetTick() / 1000;
    uint32_t hours = sec / 3600;
    uint32_t minutes = (sec % 3600) / 60;
    uint32_t seconds = sec % 60;
    const PowerStageCapabilities_t* caps = PowerStage_GetCapabilities();

    /* Reposition cursor at Row 1, Column 1 */
    printf("\033[H");
    printf("================================================================================\033[K\r\n");
    printf("      STM32G474RE + %s -- BLDC MOTOR EVALUATION DASHBOARD        \033[K\r\n",
           caps ? caps->board_name : "POWER STAGE");
    printf("================================================================================\033[K\r\n");
    printf(" Port   : LPUART1 (PA2/PA3) @ 115200 (Buffered) | CPU Clock: 170 MHz | PWM: 20 kHz\033[K\r\n");
    printf(" Uptime : %02u:%02u:%02u                         | Sample: #%05u  [Heartbeat: %c]\033[K\r\n",
           (unsigned int)hours, (unsigned int)minutes, (unsigned int)seconds, 
           (unsigned int)telem->sample_count, heartbeat);
    printf("--------------------------------------------------------------------------------\033[K\r\n");
    printf(" POWER STAGE TELEMETRY (%s - %s)\033[K\r\n",
           caps ? caps->board_name : "UNKNOWN",
           caps ? caps->driver_ic : "UNKNOWN");
    printf("--------------------------------------------------------------------------------\033[K\r\n");

    /* Bus Voltage */
    const char* vbus_status = (telem->vbus_v < 8.0f) ? "[LOW/DISCONNECTED]" : 
                              (telem->vbus_v > 48.0f) ? "[OVERVOLTAGE]" : "[OK]";
    printf(" Bus Voltage (VBUS)         : %5.2f V %s\033[K\r\n", telem->vbus_v, vbus_status);

    /* Driver Temperature */
    printf(" Driver Board Temperature   : %4.1f deg C\033[K\r\n", telem->temp_c);

    /* Speed Potentiometer */
    float pot_demanded_rpm = MOTOR_POT_MIN_RPM + (telem->pot_pct / 100.0f) * (MOTOR_POT_MAX_RPM - MOTOR_POT_MIN_RPM);
    if (ctrl->pot_control_enabled) {
        printf(" Speed Potentiometer (R42)  : %5.1f %% [ACTIVE -> Target %4d RPM]\033[K\r\n",
               telem->pot_pct, (int)pot_demanded_rpm);
    } else {
        printf(" Speed Potentiometer (R42)  : %5.1f %% [MANUAL (Press 'P' to activate)]\033[K\r\n",
               telem->pot_pct);
    }

    /* Driver Status & Fault */
    const char* drv_name = (caps && caps->board_type == BOARD_TYPE_IHM08M1_L6398) ? "L6398" : "L6230";
    printf(" Driver Status (%s DIAG) : ", drv_name);
    if (telem->fault_active) {
        printf("\033[1;31m[FAULT / OVERCURRENT TRIPPED]\033[0m\033[K\r\n");
    } else {
        printf("\033[1;32m[READY / NORMAL]\033[0m\033[K\r\n");
    }

    /* Hall Sensor Signals / Sensorless state */
    if (telem->hall_state == 7 || telem->hall_state == 0) {
        printf(" Hall Sensor Signals (H321) : [SENSORLESS MOTOR / J4 DISCONNECTED]\033[K\r\n");
    } else {
        printf(" Hall Sensor Signals (H321) : State %d [H1=%d, H2=%d, H3=%d]\033[K\r\n",
               telem->hall_state, (telem->hall_state & 1), ((telem->hall_state >> 1) & 1), ((telem->hall_state >> 2) & 1));
    }

    printf("--------------------------------------------------------------------------------\033[K\r\n");
    printf(" MOTOR DRIVE STATUS\033[K\r\n");
    printf("--------------------------------------------------------------------------------\033[K\r\n");

    /* State and Mode */
    const char* state_str = (ctrl->state == MOTOR_STATE_STOPPED) ? "STOPPED" :
                            (ctrl->state == MOTOR_STATE_STARTING) ? "STARTING (ALIGN & RAMP)" :
                            (ctrl->state == MOTOR_STATE_RUNNING) ? "RUNNING" :
                            (ctrl->state == MOTOR_STATE_BRAKING) ? "ACTIVE BRAKING" :
                            (ctrl->state == MOTOR_STATE_CHARACTERIZING) ? "CHARACTERIZING" : "FAULT";

    const char* mode_str = (ctrl->mode == MOTOR_MODE_OPEN_LOOP_CHAR) ? "STAGE 1: OPEN-LOOP V/F (SENSORLESS)" :
                           (ctrl->mode == MOTOR_MODE_SIX_STEP_VOLTAGE) ? "STAGE 2: 6-STEP VOLTAGE MODE" : "STAGE 3: 4-QUADRANT FOC";

    printf(" Drive State                : %s\033[K\r\n", state_str);
    printf(" Control Strategy           : %s\033[K\r\n", mode_str);

    /* Active Profile */
    const MotorProfile_t* active_prof = MotorProfiles_GetActive();
    uint8_t prof_idx = MotorProfiles_GetActiveIndex() + 1;
    uint8_t prof_total = MotorProfiles_GetCount();
    printf(" Active Motor Profile       : [%u/%u] %s (%u PP, Max %4.2f A)\033[K\r\n",
           prof_idx, prof_total, active_prof ? active_prof->name : "Generic",
           ctrl->pole_pairs, ctrl->torque_limit_amps);

    /* Target & Actual Speed */
    int spd_int = (int)ctrl->actual_speed_rpm;
    int tgt_int = (int)ctrl->target_speed_rpm;

    printf(" Target / Estimated Speed   : Target %4d RPM | Est %4d RPM (%s)\033[K\r\n",
           tgt_int, spd_int, (ctrl->direction > 0) ? "CW" : "CCW");
    printf(" Commanded Torque Limit     : %4.2f A (Software Regulated Ceiling)\033[K\r\n", ctrl->torque_limit_amps);
    uint16_t arr = (caps && caps->pwm_arr) ? caps->pwm_arr : 4249;
    printf(" Active Modulated Duty      : %4.1f %% (Base %4.1f %% | ARR = %u)\033[K\r\n",
           ctrl->active_duty_pct, ctrl->duty_pct, arr);

    /* Phase Currents */
    int ia_ma = (int)(telem->current_a_amps * 1000.0f);
    int ib_ma = (int)(telem->current_b_amps * 1000.0f);
    int ic_ma = (int)(telem->current_c_amps * 1000.0f);
    int ipk_ma = (int)(telem->current_peak_amps * 1000.0f);
    printf(" Phase Currents (mA)        : IA=%4d mA | IB=%4d mA | IC=%4d mA [Pk=%4d mA]\033[K\r\n",
           ia_ma, ib_ma, ic_ma, ipk_ma);

    printf(" Hardware Break Trip Limit  : %.2f A (%s OCD Break)\033[K\r\n",
           ctrl->current_limit_amps, drv_name);

    printf("================================================================================\033[K\r\n");
    printf(" INTERACTIVE COMMANDS:                                                          \033[K\r\n");
    printf("  [SPACE] Start/Stop Motor   |  [+/-] Inc/Dec Torque Limit|  [P] Toggle Pot Ctrl \033[K\r\n");
    printf("  [D] Toggle Direction       |  [[/]] Inc/Dec Base Duty   |  [R] Reset Fault     \033[K\r\n");
    printf("  [O] Next Motor Profile     |  [C] Characterize Motor    |  [B] Active Brake    \033[K\r\n");
    printf("================================================================================\033[K\r\n");
}

/**
 * @brief Render dedicated Motor Characterization Dashboard
 */
void Console_RenderCharDashboard(const MotorTelemetry_t* telem, const MotorControl_t* ctrl, char heartbeat) {
    (void)ctrl;
    uint8_t progress = MotorChar_GetProgressPct();
    const CharResults_t* res = MotorChar_GetResults();
    CharState_t state = MotorChar_GetState();
    const PowerStageCapabilities_t* caps = PowerStage_GetCapabilities();

    printf("\033[H");
    printf("================================================================================\033[K\r\n");
    printf("      STM32G474RE + %s -- MOTOR CHARACTERIZATION ROUTINE         \033[K\r\n",
           caps ? caps->board_name : "POWER STAGE");
    printf("================================================================================\033[K\r\n");
    printf(" Port   : LPUART1 (PA2/PA3) @ 115200 (Buffered) | CPU Clock: 170 MHz | PWM: 20 kHz\033[K\r\n");
    printf(" Status : %-50s [Heartbeat: %c]\033[K\r\n", res->status_msg, heartbeat);
    printf("--------------------------------------------------------------------------------\033[K\r\n");
    printf(" TEST PROGRESS: [%3u%%] [", progress);
    int bars = progress / 5;
    for (int i = 0; i < 20; i++) {
        if (i < bars) printf("=");
        else if (i == bars) printf(">");
        else printf(" ");
    }
    printf("]\033[K\r\n");
    printf("--------------------------------------------------------------------------------\033[K\r\n");
    printf(" LIVE MEASUREMENTS\033[K\r\n");
    printf("--------------------------------------------------------------------------------\033[K\r\n");
    printf(" Bus Voltage (VBUS)         : %5.2f V\033[K\r\n", telem->vbus_v);
    printf(" Peak Phase Current         : %5.3f A\033[K\r\n", telem->current_peak_amps);
    printf(" Board Temperature          : %5.1f deg C\033[K\r\n", telem->temp_c);
    printf("--------------------------------------------------------------------------------\033[K\r\n");
    printf(" CHARACTERIZATION RESULTS\033[K\r\n");
    printf("--------------------------------------------------------------------------------\033[K\r\n");
    printf(" Phase Resistance (Rs)      : %5.3f Ohm (Line-to-Line R_LL = %5.3f Ohm)\033[K\r\n",
           res->r_phase_ohm, res->r_phase_ohm * 2.0f);
    printf(" Phase Balance (U/V/W)      : U-V: %.2f | V-W: %.2f | W-U: %.2f Ohm [%.1f%% unbalance]\033[K\r\n",
           res->r_uv_ohm, res->r_vw_ohm, res->r_wu_ohm, res->phase_unbalance_pct);
    printf(" Stiction / Align Current   : %5.2f A (Min Duty: %.1f%%)\033[K\r\n",
           res->stiction_current_amps, res->min_align_duty_pct);
    printf(" Speed Constant (Kv)        : %5.0f RPM/V (Ke = %5.2f V/kRPM)\033[K\r\n",
           res->kv_rating, res->ke_v_krpm);
    printf(" Recommended Base Duty      : %5.1f %%\033[K\r\n", res->recommended_base_duty);
    printf(" Recommended V/F Slope      : %5.1f %%\033[K\r\n", res->recommended_vf_slope);
    printf("================================================================================\033[K\r\n");
    if (state == CHAR_STATE_COMPLETE) {
        printf("  \033[1;32m[ENTER] Save & Apply to Custom Profile\033[0m   |   \033[1;33m[ESC] Discard & Exit\033[0m\033[K\r\n");
    } else if (state == CHAR_STATE_ABORTED) {
        printf("  \033[1;31m[ESC] Exit Characterization Mode\033[0m\033[K\r\n");
    } else {
        printf("  \033[1;33m[ESC / X] Abort Characterization\033[0m\033[K\r\n");
    }
    printf("================================================================================\033[K\r\n");
}
