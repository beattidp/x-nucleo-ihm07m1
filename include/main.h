#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* UI & Console Configuration */
#define CONSOLE_NON_SCROLLING       1

/* Onboard Nucleo User Peripherals */
#define LD2_PIN                     GPIO_PIN_5
#define LD2_GPIO_PORT               GPIOA
#define B1_BUTTON_PIN               GPIO_PIN_13
#define B1_BUTTON_GPIO_PORT         GPIOC

/* Console LPUART1 Pins (Connected to ST-LINK V3E Virtual COM Port) */
#define CONSOLE_TX_PIN              GPIO_PIN_2
#define CONSOLE_TX_PORT             GPIOA
#define CONSOLE_TX_AF               GPIO_AF12_LPUART1

#define CONSOLE_RX_PIN              GPIO_PIN_3
#define CONSOLE_RX_PORT             GPIOA
#define CONSOLE_RX_AF               GPIO_AF12_LPUART1

/* Peripheral Handles */
extern UART_HandleTypeDef hlpuart1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim16;
extern TIM_HandleTypeDef htim2;
extern ADC_HandleTypeDef hadc1;

/* Function Prototypes */
void SystemClock_Config(void);
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
