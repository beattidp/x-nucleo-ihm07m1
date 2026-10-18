#include "main.h"
#include "console.h"
#include "power_stage.h"
#include "motor_control.h"
#include "motor_profiles.h"
#include "motor_characterization.h"

static void MX_GPIO_Init(void);

int main(void) {
    /* 1. Reset of all peripherals, Initializes Flash interface and Systick */
    HAL_Init();

    /* 2. Configure system clock to 170 MHz using HSI + PLL */
    SystemClock_Config();

    /* 3. Initialize Board GPIOs (LD2 on PA5, B1 on PC13) */
    MX_GPIO_Init();

    /* 4. Initialize LPUART1 serial console on PA2 (TX) / PA3 (RX) */
    console_init();

    /* 5. Initialize Power Stage driver and motor control engine */
    Motor_Init();

    /* Initial banner displaying detected/active power stage capabilities */
    const PowerStageCapabilities_t* caps = PowerStage_GetCapabilities();
    printf("\r\n");
    printf("================================================================================\r\n");
    printf("  STM32G474RE + %s -- 3-PHASE BLDC MOTOR EVALUATION SYSTEM        \r\n",
           caps ? caps->board_name : "POWER STAGE");
    printf("  Console : LPUART1 (PA2-TX, PA3-RX) @ 115200 baud                              \r\n");
    printf("  Driver  : %-60s\r\n", caps ? caps->driver_ic : "Unknown");
    printf("  PWM     : TIM1 CH1..3 @ 20 kHz Center-Aligned (ARR = %u)                       \r\n",
           caps ? caps->pwm_arr : 4249);
    printf("  Ratings : Max %.1f V / %.1f A Continuous (%.1f A Hardware Trip Reference)      \r\n",
           caps ? caps->max_voltage_v : 48.0f,
           caps ? caps->max_continuous_current_a : 1.4f,
           caps ? caps->peak_current_a : 2.8f);
    printf("================================================================================\r\n\r\n");

    printf("Starting non-scrolling dashboard in 2 seconds...\r\n");
    HAL_Delay(2000);

#if CONSOLE_NON_SCROLLING
    /* Clear screen, move home, hide cursor */
    printf("\033[2J\033[H\033[?25l");
#endif

    uint32_t last_dash_tick = 0;
    uint32_t last_led_tick = 0;
    static const char hb_spin[] = "|/-\\";
    uint8_t hb_idx = 0;

    while (1) {
        uint32_t now = HAL_GetTick();

        /* Handle Console Interactive Keystrokes */
        int ch = console_getch();
        if (ch != -1) {
            const MotorControl_t* ctrl = Motor_GetControl();

            /* If in characterization mode, handle characterization navigation keys */
            if (ctrl->state == MOTOR_STATE_CHARACTERIZING) {
                if (ch == 27 || ch == 'x' || ch == 'X') { /* ESC or X */
                    MotorChar_Abort("Characterization aborted by user.");
                    Motor_Stop();
                } else if (ch == '\r' || ch == '\n') {
                    if (MotorChar_GetState() == CHAR_STATE_COMPLETE) {
                        MotorChar_ApplyResults();
                        HAL_Delay(500);
                        Motor_Stop();
                    }
                }
            } else {
                switch (ch) {
                    case ' ':
                        if (ctrl->state == MOTOR_STATE_STOPPED) {
                            Motor_Start();
                        } else {
                            Motor_Stop();
                        }
                        break;
                    case '+':
                    case '=':
                        Motor_IncTorqueLimit();
                        break;
                    case '-':
                    case '_':
                        Motor_DecTorqueLimit();
                        break;
                    case ']':
                    case '}':
                        Motor_IncDuty();
                        break;
                    case '[':
                    case '{':
                        Motor_DecDuty();
                        break;
                    case 'd':
                    case 'D':
                        Motor_SetDirection(ctrl->direction > 0 ? -1 : 1);
                        break;
                    case 'm':
                    case 'M':
                        if (ctrl->mode == MOTOR_MODE_OPEN_LOOP_CHAR) {
                            Motor_SetMode(MOTOR_MODE_SIX_STEP_VOLTAGE);
                        } else {
                            Motor_SetMode(MOTOR_MODE_OPEN_LOOP_CHAR);
                        }
                        break;
                    case 'p':
                    case 'P':
                        Motor_TogglePotControl();
                        break;
                    case 'o':
                    case 'O':
                        if (ctrl->state == MOTOR_STATE_STOPPED) {
                            MotorProfiles_Next();
                        }
                        break;
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                        if (ctrl->state == MOTOR_STATE_STOPPED) {
                            MotorProfiles_Select((uint8_t)(ch - '1'));
                        }
                        break;
                    case '>':
                    case '.':
                        Motor_SetPotControl(false);
                        Motor_SetSpeed(ctrl->target_speed_rpm + 25.0f);
                        break;
                    case '<':
                    case ',':
                        Motor_SetPotControl(false);
                        if (ctrl->target_speed_rpm > 50.0f) {
                            Motor_SetSpeed(ctrl->target_speed_rpm - 25.0f);
                        }
                        break;
                    case 'z':
                    case 'Z':
                        Motor_ResetOpticalCount();
                        break;
                    case 'c':
                    case 'C':
                        Motor_StartCharacterization();
                        break;
                    case 'b':
                    case 'B':
                        Motor_Brake();
                        break;
                    case 'r':
                    case 'R':
                        Motor_ResetFault();
                        break;
                    case 'l':
                    case 'L':
                    case 0x0C: /* Ctrl-L */
                        Console_RequestFullRedraw();
                        break;
                    case '\r':
                    case '\n':
                        if (ctrl->state == MOTOR_STATE_CHARACTERIZING) {
                            MotorChar_ApplyResults();
                            Motor_Stop();
                        }
                        break;
                    case 0x1B: /* ESC */
                        if (ctrl->state == MOTOR_STATE_CHARACTERIZING) {
                            MotorChar_Abort("Cancelled by user.");
                        }
                        Motor_Stop();
                        break;
                    default:
                        break;
                }
            }
        }

        /* Check B1 Button (PC13 - Active LOW) */
        static uint8_t btn_prev = 0xFF;
        uint8_t btn_curr = (HAL_GPIO_ReadPin(B1_BUTTON_GPIO_PORT, B1_BUTTON_PIN) == GPIO_PIN_SET) ? 1 : 0;
        if (btn_prev == 0xFF) {
            btn_prev = btn_curr;
        } else if (btn_prev == 1 && btn_curr == 0) {
            /* Button pressed */
            const MotorControl_t* ctrl = Motor_GetControl();
            if (ctrl->state == MOTOR_STATE_STOPPED) {
                Motor_Start();
            } else {
                Motor_Stop();
            }
            btn_prev = btn_curr;
        } else {
            btn_prev = btn_curr;
        }

        /* Heartbeat LED Toggle (500 ms) */
        if (now - last_led_tick >= 500) {
            last_led_tick = now;
            HAL_GPIO_TogglePin(LD2_GPIO_PORT, LD2_PIN);
            hb_idx = (hb_idx + 1) % 4;
        }

        /* Refresh Dashboard at 4 Hz (250 ms) */
        if (now - last_dash_tick >= 250) {
            last_dash_tick = now;
            MotorTelemetry_t telem;
            Motor_GetTelemetry(&telem);
            const MotorControl_t* ctrl = Motor_GetControl();

#if CONSOLE_NON_SCROLLING
            if (ctrl->state == MOTOR_STATE_CHARACTERIZING) {
                Console_RenderCharDashboard(&telem, ctrl, hb_spin[hb_idx]);
            } else {
                Console_RenderDashboard(&telem, ctrl, hb_spin[hb_idx]);
            }
#else
            printf("Uptime: %lu ms | VBUS: %.2f V | Duty: %.1f %% | Current: %.2f A | Opt RPM: %.1f | Stall: %s\r\n",
                   (unsigned long)now, telem.vbus_v, ctrl->duty_pct, telem.current_peak_amps,
                   telem.optical_rpm, telem.motor_stalled ? "YES" : "NO");
#endif
        }

        HAL_Delay(5);
    }
}

/**
 * @brief Initialize onboard LD2 LED and B1 User Button
 */
static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Configure LD2 (PA5) onboard Green LED */
    HAL_GPIO_WritePin(LD2_GPIO_PORT, LD2_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LD2_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD2_GPIO_PORT, &GPIO_InitStruct);

    /* Configure B1 (PC13) onboard Blue User Button */
    GPIO_InitStruct.Pin = B1_BUTTON_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_BUTTON_GPIO_PORT, &GPIO_InitStruct);
}

/**
 * @brief System Clock Configuration (Configures MCU to 170 MHz using HSI + PLL)
 */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief Error Handler
 */
void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}

/**
 * @brief SysTick loop hook required by HAL framework
 */
void SysTick_Handler(void) {
    HAL_IncTick();
    /* Deterministic 1 kHz motor control task */
    Motor_Update1ms();
}
