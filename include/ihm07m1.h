#ifndef __IHM07M1_H
#define __IHM07M1_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim16;
extern TIM_HandleTypeDef htim2;
extern ADC_HandleTypeDef hadc1;

/* Timer Configuration */
#define IHM07M1_PWM_FREQ_HZ         20000U
#define IHM07M1_TIM1_CLK_HZ         170000000U
/* Center-aligned counter: period = (170M / 20k) / 2 = 4250 */
#define IHM07M1_PWM_ARR             (4250U - 1U)
#define IHM07M1_PWM_MAX_DUTY        IHM07M1_PWM_ARR

/* L6230 High-Side PWM Pins (TIM1 Channels 1, 2, 3) */
#define IHM07M1_IN1_PIN             GPIO_PIN_8
#define IHM07M1_IN1_PORT            GPIOA
#define IHM07M1_IN1_AF              GPIO_AF6_TIM1

#define IHM07M1_IN2_PIN             GPIO_PIN_9
#define IHM07M1_IN2_PORT            GPIOA
#define IHM07M1_IN2_AF              GPIO_AF6_TIM1

#define IHM07M1_IN3_PIN             GPIO_PIN_10
#define IHM07M1_IN3_PORT            GPIOA
#define IHM07M1_IN3_AF              GPIO_AF6_TIM1

/* L6230 Half-Bridge Enable Pins (Active High) */
#define IHM07M1_EN1_PIN             GPIO_PIN_10
#define IHM07M1_EN1_PORT            GPIOC

#define IHM07M1_EN2_PIN             GPIO_PIN_11
#define IHM07M1_EN2_PORT            GPIOC

#define IHM07M1_EN3_PIN             GPIO_PIN_12
#define IHM07M1_EN3_PORT            GPIOC

/* L6230 Fault & Emergency Break Input (TIM1_BKIN) */
#define IHM07M1_DIAG_PIN            GPIO_PIN_6
#define IHM07M1_DIAG_PORT           GPIOA
#define IHM07M1_DIAG_AF             GPIO_AF6_TIM1

/* L6230 Current Reference Generation (TIM16_CH1 on PB4) */
#define IHM07M1_REF_PIN             GPIO_PIN_4
#define IHM07M1_REF_PORT            GPIOB
#define IHM07M1_REF_AF              GPIO_AF1_TIM16
#define IHM07M1_REF_ARR             (1000U - 1U)

/* Hall / Encoder Sensor Pins (TIM2 Channels 1, 2, 3) */
#define IHM07M1_HALL1_PIN           GPIO_PIN_15
#define IHM07M1_HALL1_PORT          GPIOA
#define IHM07M1_HALL1_AF            GPIO_AF1_TIM2

#define IHM07M1_HALL2_PIN           GPIO_PIN_3
#define IHM07M1_HALL2_PORT          GPIOB
#define IHM07M1_HALL2_AF            GPIO_AF1_TIM2

#define IHM07M1_HALL3_PIN           GPIO_PIN_10
#define IHM07M1_HALL3_PORT          GPIOB
#define IHM07M1_HALL3_AF            GPIO_AF1_TIM2

/* Onboard IHM07M1 Red User LED (D11) */
#define IHM07M1_RED_LED_PIN         GPIO_PIN_2
#define IHM07M1_RED_LED_PORT        GPIOB

/* Analog Feedback Channels (ADC1) */
#define IHM07M1_ADC_CURR_A_PIN      GPIO_PIN_0
#define IHM07M1_ADC_CURR_A_PORT     GPIOA
#define IHM07M1_ADC_CURR_A_CH       ADC_CHANNEL_1

#define IHM07M1_ADC_VBUS_PIN        GPIO_PIN_1
#define IHM07M1_ADC_VBUS_PORT       GPIOA
#define IHM07M1_ADC_VBUS_CH         ADC_CHANNEL_2

#define IHM07M1_ADC_TEMP_PIN        GPIO_PIN_2
#define IHM07M1_ADC_TEMP_PORT       GPIOC
#define IHM07M1_ADC_TEMP_CH         ADC_CHANNEL_8

#define IHM07M1_ADC_POT_PIN         GPIO_PIN_1
#define IHM07M1_ADC_POT_PORT        GPIOB
#define IHM07M1_ADC_POT_CH          ADC_CHANNEL_12

#define IHM07M1_ADC_CURR_B_PIN      GPIO_PIN_1
#define IHM07M1_ADC_CURR_B_PORT     GPIOC
#define IHM07M1_ADC_CURR_B_CH       ADC_CHANNEL_7

#define IHM07M1_ADC_CURR_C_PIN      GPIO_PIN_0
#define IHM07M1_ADC_CURR_C_PORT     GPIOC
#define IHM07M1_ADC_CURR_C_CH       ADC_CHANNEL_6

/* Scaling Factors */
/* VBUS: (169k + 9.31k)/9.31k = 19.1525, VREF = 3.3V, 12-bit ADC (4095) */
#define IHM07M1_VBUS_DIVIDER_RATIO  19.1525f
/* Current: Shunt = 0.33 Ohm, TSV994 Op-Amp Gain = 1.53 -> 0.5049 V/A */
#define IHM07M1_CURRENT_SENSE_GAIN  0.5049f

#include "power_stage.h"

/* Driver API Functions */
void IHM07M1_Init(void);
void IHM07M1_SetCurrentReference(float max_amps);
void IHM07M1_EnablePhase(uint8_t phase_mask); /* Bit0=U, Bit1=V, Bit2=W */
void IHM07M1_DisableBridge(void);
void IHM07M1_SetPWM(uint16_t duty_u, uint16_t duty_v, uint16_t duty_w);
void IHM07M1_StartPWM(void);
void IHM07M1_StopPWM(void);
bool IHM07M1_IsFaultActive(void);
void IHM07M1_ClearFault(void);
void IHM07M1_SetRedLED(bool state);
void IHM07M1_ToggleRedLED(void);
uint8_t IHM07M1_ReadHallSensors(void);
const PowerStageCapabilities_t* IHM07M1_GetCapabilities(void);

/* Analog Telemetry & Offset Calibration */
float IHM07M1_ReadVBusVoltage(void);
float IHM07M1_ReadTemperature(void);
float IHM07M1_ReadPotentiometer(void);
void  IHM07M1_ReadRawCurrentSensors(uint16_t* raw_a, uint16_t* raw_b, uint16_t* raw_c);
void  IHM07M1_CalibrateCurrentOffsets(uint16_t* offset_a, uint16_t* offset_b, uint16_t* offset_c);

#ifdef __cplusplus
}
#endif

#endif /* __IHM07M1_H */
