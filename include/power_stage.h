/**
 * @file power_stage.h
 * @brief Unified Hardware Abstraction Layer (HAL) for 3-Phase BLDC Power Stages
 *
 * Supports:
 *  - X-NUCLEO-IHM07M1 (L6230 DMOS 3-Phase Bridge, 1.4A RMS / 2.8A peak)
 *  - X-NUCLEO-IHM08M1 (L6398 Gate Drivers + STL220N6F7 discrete MOSFETs, 15A continuous / 30A peak)
 */

#ifndef __POWER_STAGE_H
#define __POWER_STAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* Supported Power Stage Expansion Boards */
#define BOARD_TYPE_IHM07M1_L6230     0
#define BOARD_TYPE_IHM08M1_L6398     1
typedef uint8_t PowerStageBoardType_t;

/* Default Active Board Selection */
#ifndef TARGET_POWER_STAGE
#define TARGET_POWER_STAGE BOARD_TYPE_IHM07M1_L6230
#endif

/* Power Stage Hardware Capabilities & Telemetry Parameters */
typedef struct {
    PowerStageBoardType_t board_type;
    const char*           board_name;             /* e.g. "X-NUCLEO-IHM07M1" */
    const char*           driver_ic;              /* e.g. "L6230 DMOS 3-Phase Bridge" */
    float                 max_voltage_v;          /* Absolute max supply voltage (V) */
    float                 nominal_voltage_v;      /* Typical operating supply voltage (V) */
    float                 max_continuous_current_a;/* Max continuous phase current rating (A) */
    float                 peak_current_a;         /* Max peak current before hardware trip (A) */
    uint8_t               shunt_count;            /* 1 or 3 shunts */
    float                 shunt_resistance_ohm;   /* Shunt resistance in Ohms */
    float                 opamp_gain;             /* Current sensing amplification gain */
    float                 current_sense_gain;     /* Overall sensitivity (V/A) */
    float                 vbus_divider_ratio;     /* Resistor divider ratio on VBUS pin */
    uint16_t              pwm_arr;                /* Timer Auto-Reload Register for 20 kHz */
} PowerStageCapabilities_t;

/* Phase Enable Mask Bits */
#define POWER_STAGE_PHASE_U_ENABLE   (1U << 0)
#define POWER_STAGE_PHASE_V_ENABLE   (1U << 1)
#define POWER_STAGE_PHASE_W_ENABLE   (1U << 2)
#define POWER_STAGE_ALL_PHASES       (0x07U)

/* Core Driver Control API */
void PowerStage_Init(void);
void PowerStage_SetPWM(uint16_t duty_u, uint16_t duty_v, uint16_t duty_w);
void PowerStage_StartPWM(void);
void PowerStage_StopPWM(void);
void PowerStage_EnablePhase(uint8_t phase_mask);
void PowerStage_DisableBridge(void);
bool PowerStage_IsFaultActive(void);
void PowerStage_ClearFault(void);
void PowerStage_SetCurrentTripThreshold(float max_amps);
uint8_t PowerStage_ReadHallSensors(void);
void PowerStage_SetStatusLED(bool state);
void PowerStage_ToggleStatusLED(void);
const PowerStageCapabilities_t* PowerStage_GetCapabilities(void);

/* Synchronous Telemetry & Analog Sensing API */
float PowerStage_ReadVBusVoltage(void);
float PowerStage_ReadTemperature(void);
float PowerStage_ReadPotentiometer(void);
void  PowerStage_ReadRawCurrentSensors(uint16_t* raw_a, uint16_t* raw_b, uint16_t* raw_c);
void  PowerStage_CalibrateCurrentOffsets(uint16_t* offset_a, uint16_t* offset_b, uint16_t* offset_c);

#ifdef __cplusplus
}
#endif

#endif /* __POWER_STAGE_H */
