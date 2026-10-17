/**
 * @file driver_l6398_ihm08m1.h
 * @brief Power Stage Driver for X-NUCLEO-IHM08M1 (L6398 Gate Drivers + STL220N6F7 MOSFETs)
 */

#ifndef __DRIVER_L6398_IHM08M1_H
#define __DRIVER_L6398_IHM08M1_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "power_stage.h"

/* Timer and PWM Constants for IHM08M1 */
#define IHM08M1_PWM_FREQ_HZ         20000U
#define IHM08M1_TIM1_CLK_HZ         170000000U
#define IHM08M1_PWM_ARR             (4250U - 1U)
#define IHM08M1_DEADTIME_COUNTS     120U /* ~705 ns deadtime at 170 MHz */

/* Driver API Functions */
void IHM08M1_Init(void);
void IHM08M1_SetPWM(uint16_t duty_u, uint16_t duty_v, uint16_t duty_w);
void IHM08M1_StartPWM(void);
void IHM08M1_StopPWM(void);
void IHM08M1_EnablePhase(uint8_t phase_mask);
void IHM08M1_DisableBridge(void);
bool IHM08M1_IsFaultActive(void);
void IHM08M1_ClearFault(void);
void IHM08M1_SetCurrentTripThreshold(float max_amps);
uint8_t IHM08M1_ReadHallSensors(void);
void IHM08M1_SetStatusLED(bool state);
void IHM08M1_ToggleStatusLED(void);
const PowerStageCapabilities_t* IHM08M1_GetCapabilities(void);

float IHM08M1_ReadVBusVoltage(void);
float IHM08M1_ReadTemperature(void);
float IHM08M1_ReadPotentiometer(void);
void  IHM08M1_ReadRawCurrentSensors(uint16_t* raw_a, uint16_t* raw_b, uint16_t* raw_c);
void  IHM08M1_CalibrateCurrentOffsets(uint16_t* offset_a, uint16_t* offset_b, uint16_t* offset_c);

#ifdef __cplusplus
}
#endif

#endif /* __DRIVER_L6398_IHM08M1_H */
