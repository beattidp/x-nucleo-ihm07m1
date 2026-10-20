#include "console.h"
#include "motor_control.h"
#include "motor_profiles.h"
#include "motor_characterization.h"
#include "power_stage.h"
#include "ihm07m1.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

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

/* Active dashboard mode: 0 = uninitialized, 1 = main, 2 = characterization */
static uint8_t s_active_dashboard_mode = 0;
static bool s_force_full_redraw = true;

void Console_RequestFullRedraw(void) {
    s_force_full_redraw = true;
}

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

    /* Disable libc stdio buffering so partial updates flush immediately */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

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

/* ============================================================================
 * STATIC BACKGROUND TEMPLATES (78 chars wide to prevent terminal line-wrap)
 * ============================================================================ */

static void Console_RenderMainStaticTemplate(const PowerStageCapabilities_t* caps) {
    printf("\033[?25l");      /* Hide terminal cursor */
    printf("\033[2J\033[H");  /* Clear entire screen and home cursor */
    printf("==============================================================================\r\n");
    printf("   STM32G474RE + %-16s -- BLDC MOTOR EVALUATION DASHBOARD\r\n",
           caps ? caps->board_name : "POWER STAGE");
    printf("==============================================================================\r\n");
    printf(" Port   : LPUART1 (PA2/PA3) @ 115200 | Clock: 170 MHz | PWM: 20 kHz\r\n");
    printf(" Uptime :                                | \r\n");
    printf("------------------------------------------------------------------------------\r\n");
    printf(" POWER STAGE TELEMETRY (%s - %s)\r\n",
           caps ? caps->board_name : "UNKNOWN",
           caps ? caps->driver_ic : "UNKNOWN");
    printf("------------------------------------------------------------------------------\r\n");
    printf(" Bus Voltage (VBUS)         : \r\n");
    printf(" Driver Board Temperature   : \r\n");
    printf(" Speed Potentiometer (R42)  : \r\n");
    printf(" Driver Status (%-5s DIAG) : \r\n", (caps && caps->board_type == BOARD_TYPE_IHM08M1_L6398) ? "L6398" : "L6230");
    printf(" Hall Sensor Signals (H321) : \r\n");
#if (OPTICAL_TACH_USE_CN10_PB11 == 1)
    printf(" Optical Tachometer [CN10-18]: \r\n");
#else
    printf(" Optical Tachometer (J3-Z+) : \r\n");
#endif
    printf("------------------------------------------------------------------------------\r\n");
    printf(" MOTOR DRIVE STATUS\r\n");
    printf("------------------------------------------------------------------------------\r\n");
    printf(" Drive State                : \r\n");
    printf(" Control Strategy           : \r\n");
    printf(" Active Motor Profile       : \r\n");
    printf(" Target / Estimated Speed   : \r\n");
    printf(" Commanded Torque Limit     : \r\n");
    printf(" Active Modulated Duty      : \r\n");
    printf(" Phase & FOC Currents (mA)  : \r\n");
    printf(" HW Trip Limit & Peak Curr  : \r\n");
    printf("  [SPACE] Run/Stop | [1-4] Profile | [< / >] Speed | [+/-] Torque | [P] Pot  \r\n");
    printf("  [F] FOC Loop     | [S<rpm>] Speed| [B] Brake     | [Z] Zero Rev | [R] Reset\r\n");
    printf("==============================================================================\r\n");
}

static void Console_RenderCharStaticTemplate(const PowerStageCapabilities_t* caps) {
    printf("\033[?25l");      /* Hide terminal cursor */
    printf("\033[2J\033[H");  /* Clear entire screen and home cursor */
    printf("==============================================================================\r\n");
    printf("   STM32G474RE + %-16s -- MOTOR CHARACTERIZATION ROUTINE\r\n",
           caps ? caps->board_name : "POWER STAGE");
    printf("==============================================================================\r\n");
    printf(" Port   : LPUART1 (PA2/PA3) @ 115200 | Clock: 170 MHz | PWM: 20 kHz\r\n");
    printf(" Status : \r\n");
    printf("------------------------------------------------------------------------------\r\n");
    printf(" TEST PROGRESS: [    ] [                    ]\r\n");
    printf("------------------------------------------------------------------------------\r\n");
    printf(" LIVE MEASUREMENTS\r\n");
    printf("------------------------------------------------------------------------------\r\n");
    printf(" Bus Voltage (VBUS)         : \r\n");
    printf(" Peak Phase Current         : \r\n");
    printf(" Board Temperature          : \r\n");
    printf("------------------------------------------------------------------------------\r\n");
    printf(" CHARACTERIZATION RESULTS\r\n");
    printf("------------------------------------------------------------------------------\r\n");
    printf(" Phase Resistance (Rs)      : \r\n");
    printf(" Phase Balance (U/V/W)      : \r\n");
    printf(" Stiction / Align Current   : \r\n");
    printf(" Speed Constant (Kv)        : \r\n");
    printf(" Recommended Base Duty      : \r\n");
    printf(" Recommended V/F Slope      : \r\n");
    printf("==============================================================================\r\n");
    printf("                                                                              \r\n");
    printf("==============================================================================\r\n");
}

/* ============================================================================
 * SHADOW CACHE FOR DIFFERENTIAL PARTIAL REFRESH
 * ============================================================================ */

typedef struct {
    bool valid;
    uint32_t last_sec;
    float vbus_v;
    float temp_c;
    float pot_pct;
    float pot_target_rpm;
    bool pot_ctrl;
    bool fault_active;
    uint8_t hall_state;
    int optical_rpm;
    uint32_t optical_pulse_count;
    bool motor_stalled;
    bool motor_slipping;
    MotorState_t state;
    MotorMode_t mode;
    bool foc_loop;
    uint8_t prof_idx;
    int target_speed_rpm;
    int actual_speed_rpm;
    int line21_optical_rpm;
    int8_t direction;
    float torque_limit_amps;
    float active_duty_pct;
    float duty_pct;
    int ia_ma;
    int ib_ma;
    int ic_ma;
    int ipk_ma;
    int id_ma;
    int iq_ma;
    float current_limit_amps;
} MainShadow_t;

static MainShadow_t s_main_shadow = {0};

typedef struct {
    bool valid;
    char status_msg[64];
    uint8_t progress;
    float vbus_v;
    float peak_curr;
    float temp_c;
    float r_phase;
    float r_uv;
    float r_vw;
    float r_wu;
    float unbalance;
    float stiction_curr;
    float min_duty;
    float kv;
    float ke;
    float base_duty;
    float vf_slope;
    CharState_t state;
} CharShadow_t;

static CharShadow_t s_char_shadow = {0};

/* ============================================================================
 * DIFFERENTIAL PARTIAL REFRESH RENDERERS
 * ============================================================================ */

/**
 * @brief Render differential partial refresh of the main motor evaluation dashboard
 *        Only changes are written across UART using ANSI cursor positioning (\033[<row>;<col>H).
 *        Typical 250ms refresh payload is ~120-180 bytes (<1.5 ms UART time, <6% link bandwidth).
 */
void Console_RenderDashboard(const MotorTelemetry_t* telem, const MotorControl_t* ctrl, char heartbeat) {
    const PowerStageCapabilities_t* caps = PowerStage_GetCapabilities();

    /* Check if full static background redraw is needed */
    if (s_active_dashboard_mode != 1 || s_force_full_redraw) {
        Console_RenderMainStaticTemplate(caps);
        s_active_dashboard_mode = 1;
        s_force_full_redraw = false;
        s_main_shadow.valid = false;
    }

    /* 1. Master Sample Synchronization Anchor: Row 5, Col 44
     *    Emitted every cycle to act as the clock/epoch delimiter for stream log parsers */
    printf("\033[5;44HSample: #%07lu  [Heartbeat: %c]", (unsigned long)telem->sample_count, heartbeat);

    /* 2. Uptime: Row 5, Col 11 */
    uint32_t sec = HAL_GetTick() / 1000;
    if (!s_main_shadow.valid || sec != s_main_shadow.last_sec) {
        uint32_t hours = sec / 3600;
        uint32_t minutes = (sec % 3600) / 60;
        uint32_t seconds = sec % 60;
        printf("\033[5;11H%02u:%02u:%02u", (unsigned int)hours, (unsigned int)minutes, (unsigned int)seconds);
        s_main_shadow.last_sec = sec;
    }

    /* 3. Bus Voltage (VBUS): Row 9, Col 31 */
    if (!s_main_shadow.valid || fabsf(telem->vbus_v - s_main_shadow.vbus_v) >= 0.05f) {
        const char* vbus_status = (telem->vbus_v < 8.0f) ? "[LOW/DISCONNECTED]" : 
                                  (telem->vbus_v > 48.0f) ? "[OVERVOLTAGE]     " : "[OK]              ";
        printf("\033[9;31H%5.2f V %-18s", telem->vbus_v, vbus_status);
        s_main_shadow.vbus_v = telem->vbus_v;
    }

    /* 4. Driver Board Temperature: Row 10, Col 31 */
    if (!s_main_shadow.valid || fabsf(telem->temp_c - s_main_shadow.temp_c) >= 0.2f) {
        printf("\033[10;31H%4.1f deg C", telem->temp_c);
        s_main_shadow.temp_c = telem->temp_c;
    }

    /* 5. Speed Potentiometer (R42): Row 11, Col 31 */
    if (!s_main_shadow.valid || 
        fabsf(telem->pot_pct - s_main_shadow.pot_pct) >= 0.2f || 
        fabsf(telem->pot_target_rpm - s_main_shadow.pot_target_rpm) >= 1.0f ||
        ctrl->pot_control_enabled != s_main_shadow.pot_ctrl) {
        if (ctrl->pot_control_enabled) {
            printf("\033[11;31H%5.1f %% [ACTIVE -> Target %4d RPM]   ",
                   telem->pot_pct, (int)telem->pot_target_rpm);
        } else {
            printf("\033[11;31H%5.1f %% [MANUAL (Press 'P' to enable)]",
                   telem->pot_pct);
        }
        s_main_shadow.pot_pct = telem->pot_pct;
        s_main_shadow.pot_target_rpm = telem->pot_target_rpm;
        s_main_shadow.pot_ctrl = ctrl->pot_control_enabled;
    }

    /* 6. Driver Status (DIAG): Row 12, Col 31 */
    if (!s_main_shadow.valid || telem->fault_active != s_main_shadow.fault_active) {
        if (telem->fault_active) {
            printf("\033[12;31H\033[1;31m[FAULT / OVERCURRENT TRIPPED]\033[0m");
        } else {
            printf("\033[12;31H\033[1;32m[READY / NORMAL]\033[0m             ");
        }
        s_main_shadow.fault_active = telem->fault_active;
    }

    /* 7. Hall Sensor Signals: Row 13, Col 31 */
    if (!s_main_shadow.valid || telem->hall_state != s_main_shadow.hall_state) {
        if (telem->hall_state == 7 || telem->hall_state == 0) {
            printf("\033[13;31H[SENSORLESS MOTOR / J4 DISCONNECTED] ");
        } else {
            printf("\033[13;31HState %d [H1=%d, H2=%d, H3=%d]           ",
                   telem->hall_state, (telem->hall_state & 1), 
                   ((telem->hall_state >> 1) & 1), ((telem->hall_state >> 2) & 1));
        }
        s_main_shadow.hall_state = telem->hall_state;
    }

    /* 7.5. Optical Tachometer: Row 14, Col 31 */
    int opt_rpm_int = (int)telem->optical_rpm;
    if (!s_main_shadow.valid || opt_rpm_int != s_main_shadow.optical_rpm ||
        telem->optical_pulse_count != s_main_shadow.optical_pulse_count ||
        telem->motor_stalled != s_main_shadow.motor_stalled ||
        telem->motor_slipping != s_main_shadow.motor_slipping) {
        const char* status_tag;
        if (telem->motor_stalled) {
            status_tag = "\033[1;31m[STALLED / LOCKED]\033[0m   ";
        } else if (telem->motor_slipping) {
            status_tag = "\033[1;33m[SLIPPING / VIBRATING]\033[0m";
        } else if (opt_rpm_int > 10) {
            status_tag = "\033[1;32m[ROTATING - OK]\033[0m      ";
        } else if (ctrl->state == MOTOR_STATE_STOPPED) {
            status_tag = "[STOPPED]            ";
        } else if (ctrl->state == MOTOR_STATE_STARTING) {
            status_tag = "[ALIGN / RAMPING]    ";
        } else {
            status_tag = "[WAITING FOR PULSE]  ";
        }
        printf("\033[14;31H%4d RPM (Revs: %5lu) %s",
               opt_rpm_int, (unsigned long)telem->optical_pulse_count, status_tag);
        s_main_shadow.optical_rpm = opt_rpm_int;
        s_main_shadow.optical_pulse_count = telem->optical_pulse_count;
        s_main_shadow.motor_stalled = telem->motor_stalled;
        s_main_shadow.motor_slipping = telem->motor_slipping;
    }

    /* 8. Drive State: Row 18, Col 31 */
    if (!s_main_shadow.valid || ctrl->state != s_main_shadow.state) {
        const char* state_str = (ctrl->state == MOTOR_STATE_STOPPED) ? "STOPPED" :
                                (ctrl->state == MOTOR_STATE_STARTING) ? "STARTING (ALIGN & RAMP)" :
                                (ctrl->state == MOTOR_STATE_RUNNING) ? "RUNNING" :
                                (ctrl->state == MOTOR_STATE_BRAKING) ? "ACTIVE BRAKING" :
                                (ctrl->state == MOTOR_STATE_CHARACTERIZING) ? "CHARACTERIZING" : "FAULT";
        printf("\033[18;31H%-35s", state_str);
        s_main_shadow.state = ctrl->state;
    }

    /* 9. Control Strategy: Row 19, Col 31 */
    if (!s_main_shadow.valid || ctrl->mode != s_main_shadow.mode || ctrl->foc_closed_loop != s_main_shadow.foc_loop) {
        const char* mode_str;
        if (ctrl->mode == MOTOR_MODE_SIX_STEP_VOLTAGE) {
            mode_str = "STAGE 2: 6-STEP VOLTAGE MODE       ";
        } else if (ctrl->foc_closed_loop) {
            mode_str = "\033[1;32mSTAGE 3: FOC DUAL PI REGULATOR (ON)\033[0m ";
        } else {
            mode_str = "STAGE 1: OPEN-LOOP V/F (PRESS 'F') ";
        }
        printf("\033[19;31H%-35s", mode_str);
        s_main_shadow.mode = ctrl->mode;
        s_main_shadow.foc_loop = ctrl->foc_closed_loop;
    }

    /* 10. Active Motor Profile: Row 20, Col 31 */
    uint8_t prof_idx = MotorProfiles_GetActiveIndex() + 1;
    if (!s_main_shadow.valid || prof_idx != s_main_shadow.prof_idx) {
        const MotorProfile_t* active_prof = MotorProfiles_GetActive();
        uint8_t prof_total = MotorProfiles_GetCount();
        printf("\033[20;31H[%u/%u] %s (%u PP, Max %4.2f A)%-6s",
               prof_idx, prof_total, active_prof ? active_prof->name : "Generic",
               ctrl->pole_pairs, ctrl->torque_limit_amps, "");
        s_main_shadow.prof_idx = prof_idx;
    }

    /* 11. Target & Actual Speed: Row 21, Col 31 */
    int spd_int = (int)ctrl->actual_speed_rpm;
    int tgt_int = (int)ctrl->target_speed_rpm;
    if (!s_main_shadow.valid || spd_int != s_main_shadow.actual_speed_rpm || 
        tgt_int != s_main_shadow.target_speed_rpm || ctrl->direction != s_main_shadow.direction ||
        opt_rpm_int != s_main_shadow.line21_optical_rpm) {
        printf("\033[21;31HTarget %4d | Est %4d | Real %4d RPM (%s)",
               tgt_int, spd_int, opt_rpm_int, (ctrl->direction > 0) ? "CW " : "CCW");
        s_main_shadow.actual_speed_rpm = spd_int;
        s_main_shadow.target_speed_rpm = tgt_int;
        s_main_shadow.direction = ctrl->direction;
        s_main_shadow.line21_optical_rpm = opt_rpm_int;
    }

    /* 12. Commanded Torque Limit: Row 22, Col 31 */
    if (!s_main_shadow.valid || fabsf(ctrl->torque_limit_amps - s_main_shadow.torque_limit_amps) >= 0.05f) {
        printf("\033[22;31H%4.2f A (Software Regulated Ceiling)", ctrl->torque_limit_amps);
        s_main_shadow.torque_limit_amps = ctrl->torque_limit_amps;
    }

    /* 13. Active Modulated Duty: Row 23, Col 31 */
    if (!s_main_shadow.valid || fabsf(ctrl->active_duty_pct - s_main_shadow.active_duty_pct) >= 0.2f ||
        fabsf(ctrl->duty_pct - s_main_shadow.duty_pct) >= 0.2f) {
        uint16_t arr = (caps && caps->pwm_arr) ? caps->pwm_arr : 4249;
        printf("\033[23;31H%4.1f %% (Base %4.1f %% | ARR = %u)  ",
               ctrl->active_duty_pct, ctrl->duty_pct, arr);
        s_main_shadow.active_duty_pct = ctrl->active_duty_pct;
        s_main_shadow.duty_pct = ctrl->duty_pct;
    }

    /* 14. Phase & FOC DQ Currents (mA): Row 24, Col 31 */
    int ia_ma = (int)(telem->current_a_amps * 1000.0f);
    int ib_ma = (int)(telem->current_b_amps * 1000.0f);
    int ic_ma = (int)(telem->current_c_amps * 1000.0f);
    int ipk_ma = (int)(telem->current_peak_amps * 1000.0f);
    int id_ma = (int)(telem->id_amps * 1000.0f);
    int iq_ma = (int)(telem->iq_amps * 1000.0f);
    if (!s_main_shadow.valid || abs(ia_ma - s_main_shadow.ia_ma) >= 5 ||
        abs(ib_ma - s_main_shadow.ib_ma) >= 5 || abs(ic_ma - s_main_shadow.ic_ma) >= 5 ||
        abs(id_ma - s_main_shadow.id_ma) >= 5 || abs(iq_ma - s_main_shadow.iq_ma) >= 5) {
        printf("\033[24;31HIA=%4d IB=%4d IC=%4d | Id=%4d Iq=%4d mA  ",
               ia_ma, ib_ma, ic_ma, id_ma, iq_ma);
        s_main_shadow.ia_ma = ia_ma;
        s_main_shadow.ib_ma = ib_ma;
        s_main_shadow.ic_ma = ic_ma;
        s_main_shadow.id_ma = id_ma;
        s_main_shadow.iq_ma = iq_ma;
    }

    /* 15. Hardware Break Trip Limit & Peak Current: Row 25, Col 31 */
    if (!s_main_shadow.valid || fabsf(ctrl->current_limit_amps - s_main_shadow.current_limit_amps) >= 0.05f ||
        abs(ipk_ma - s_main_shadow.ipk_ma) >= 5) {
        const char* drv_name = (caps && caps->board_type == BOARD_TYPE_IHM08M1_L6398) ? "L6398" : "L6230";
        printf("\033[25;31H%.2f A (%s OCD) | Peak: %4d mA  ",
               ctrl->current_limit_amps, drv_name, ipk_ma);
        s_main_shadow.current_limit_amps = ctrl->current_limit_amps;
        s_main_shadow.ipk_ma = ipk_ma;
    }

    /* Cleanly park cursor below dashboard and flush immediately */
    printf("\033[30;1H");
    fflush(stdout);
    s_main_shadow.valid = true;
}

/**
 * @brief Render differential partial refresh of the motor characterization dashboard
 */
void Console_RenderCharDashboard(const MotorTelemetry_t* telem, const MotorControl_t* ctrl, char heartbeat) {
    (void)ctrl;
    const PowerStageCapabilities_t* caps = PowerStage_GetCapabilities();
    uint8_t progress = MotorChar_GetProgressPct();
    const CharResults_t* res = MotorChar_GetResults();
    CharState_t state = MotorChar_GetState();

    /* Check if full static background redraw is needed */
    if (s_active_dashboard_mode != 2 || s_force_full_redraw) {
        Console_RenderCharStaticTemplate(caps);
        s_active_dashboard_mode = 2;
        s_force_full_redraw = false;
        s_char_shadow.valid = false;
    }

    /* 1. Status Message & Heartbeat: Row 5, Col 11 & Col 63 */
    if (!s_char_shadow.valid || strcmp(res->status_msg, s_char_shadow.status_msg) != 0) {
        printf("\033[5;11H%-50s", res->status_msg);
        strncpy(s_char_shadow.status_msg, res->status_msg, sizeof(s_char_shadow.status_msg) - 1);
    }
    printf("\033[5;63H[Heartbeat: %c]", heartbeat);

    /* 2. Test Progress: Row 7, Col 17 */
    if (!s_char_shadow.valid || progress != s_char_shadow.progress) {
        printf("\033[7;17H[%3u%%] [", progress);
        int bars = progress / 5;
        for (int i = 0; i < 20; i++) {
            if (i < bars) printf("=");
            else if (i == bars) printf(">");
            else printf(" ");
        }
        printf("]");
        s_char_shadow.progress = progress;
    }

    /* 3. Live Measurements: Rows 11-13, Col 31 */
    if (!s_char_shadow.valid || fabsf(telem->vbus_v - s_char_shadow.vbus_v) >= 0.05f) {
        printf("\033[11;31H%5.2f V", telem->vbus_v);
        s_char_shadow.vbus_v = telem->vbus_v;
    }
    if (!s_char_shadow.valid || fabsf(telem->current_peak_amps - s_char_shadow.peak_curr) >= 0.01f) {
        printf("\033[12;31H%5.3f A", telem->current_peak_amps);
        s_char_shadow.peak_curr = telem->current_peak_amps;
    }
    if (!s_char_shadow.valid || fabsf(telem->temp_c - s_char_shadow.temp_c) >= 0.2f) {
        printf("\033[13;31H%5.1f deg C", telem->temp_c);
        s_char_shadow.temp_c = telem->temp_c;
    }

    /* 4. Results: Rows 17-22, Col 31 */
    if (!s_char_shadow.valid || fabsf(res->r_phase_ohm - s_char_shadow.r_phase) >= 0.001f) {
        printf("\033[17;31H%5.3f Ohm (Line-to-Line R_LL = %5.3f Ohm)",
               res->r_phase_ohm, res->r_phase_ohm * 2.0f);
        s_char_shadow.r_phase = res->r_phase_ohm;
    }
    if (!s_char_shadow.valid || fabsf(res->phase_unbalance_pct - s_char_shadow.unbalance) >= 0.1f) {
        printf("\033[18;31HU-V: %.2f | V-W: %.2f | W-U: %.2f Ohm [%.1f%% unbalance]",
               res->r_uv_ohm, res->r_vw_ohm, res->r_wu_ohm, res->phase_unbalance_pct);
        s_char_shadow.unbalance = res->phase_unbalance_pct;
    }
    if (!s_char_shadow.valid || fabsf(res->stiction_current_amps - s_char_shadow.stiction_curr) >= 0.01f) {
        printf("\033[19;31H%5.2f A (Min Duty: %.1f%%)",
               res->stiction_current_amps, res->min_align_duty_pct);
        s_char_shadow.stiction_curr = res->stiction_current_amps;
    }
    if (!s_char_shadow.valid || fabsf(res->kv_rating - s_char_shadow.kv) >= 1.0f) {
        printf("\033[20;31H%5.0f RPM/V (Ke = %5.2f V/kRPM)",
               res->kv_rating, res->ke_v_krpm);
        s_char_shadow.kv = res->kv_rating;
    }
    if (!s_char_shadow.valid || fabsf(res->recommended_base_duty - s_char_shadow.base_duty) >= 0.1f) {
        printf("\033[21;31H%5.1f %%", res->recommended_base_duty);
        s_char_shadow.base_duty = res->recommended_base_duty;
    }
    if (!s_char_shadow.valid || fabsf(res->recommended_vf_slope - s_char_shadow.vf_slope) >= 0.1f) {
        printf("\033[22;31H%5.1f %%", res->recommended_vf_slope);
        s_char_shadow.vf_slope = res->recommended_vf_slope;
    }

    /* 5. Key actions: Row 24 */
    if (!s_char_shadow.valid || state != s_char_shadow.state) {
        if (state == CHAR_STATE_COMPLETE) {
            printf("\033[24;1H  \033[1;32m[ENTER] Save & Apply to Custom Profile\033[0m   |   \033[1;33m[ESC] Discard & Exit\033[0m\033[K");
        } else if (state == CHAR_STATE_ABORTED) {
            printf("\033[24;1H  \033[1;31m[ESC] Exit Characterization Mode\033[0m\033[K");
        } else {
            printf("\033[24;1H  \033[1;33m[ESC / X] Abort Characterization\033[0m\033[K");
        }
        s_char_shadow.state = state;
    }

    /* Park cursor cleanly below char dashboard and flush immediately */
    printf("\033[26;1H");
    fflush(stdout);
    s_char_shadow.valid = true;
}
