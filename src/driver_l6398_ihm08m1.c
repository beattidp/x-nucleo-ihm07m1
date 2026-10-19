/**
 * @file driver_l6398_ihm08m1.c
 * @brief Power Stage Driver for X-NUCLEO-IHM08M1 (L6398 Gate Drivers + STL220N6F7 MOSFETs)
 */

#include "driver_l6398_ihm08m1.h"
#include "power_stage.h"
#include "stm32g4xx_ll_adc.h"

static const PowerStageCapabilities_t s_ihm08m1_caps = {
    .board_type = BOARD_TYPE_IHM08M1_L6398,
    .board_name = "X-NUCLEO-IHM08M1",
    .driver_ic = "L6398 Gate Drivers + STL220N6F7 FETs",
    .max_voltage_v = 48.0f,
    .nominal_voltage_v = 24.0f,
    .max_continuous_current_a = 15.0f,
    .peak_current_a = 30.0f,
    .shunt_count = 3,
    .shunt_resistance_ohm = 0.010f,  /* 10 mOhm shunts */
    .opamp_gain = 5.7f,              /* 5.7x current sense gain */
    .current_sense_gain = 0.0570f,   /* 0.057 V/A */
    .vbus_divider_ratio = 9.30f,     /* 120k / 15k divider */
    .pwm_arr = IHM08M1_PWM_ARR
};

const PowerStageCapabilities_t* IHM08M1_GetCapabilities(void) {
    return &s_ihm08m1_caps;
}

void IHM08M1_Init(void) {
    /* Clocks, GPIOs, complementary TIM1, and ADC initialization for IHM08M1 */
}

void IHM08M1_SetPWM(uint16_t duty_u, uint16_t duty_v, uint16_t duty_w) {
    (void)duty_u;
    (void)duty_v;
    (void)duty_w;
}

void IHM08M1_StartPWM(void) {
}

void IHM08M1_StopPWM(void) {
}

void IHM08M1_EnablePhase(uint8_t phase_mask) {
    (void)phase_mask;
}

void IHM08M1_DisableBridge(void) {
}

bool IHM08M1_IsFaultActive(void) {
    return false;
}

void IHM08M1_ClearFault(void) {
}

void IHM08M1_SetCurrentTripThreshold(float max_amps) {
    (void)max_amps;
}

uint8_t IHM08M1_ReadHallSensors(void) {
    return 7;
}

void IHM08M1_SetStatusLED(bool state) {
    (void)state;
}

void IHM08M1_ToggleStatusLED(void) {
}

float IHM08M1_ReadVBusVoltage(void) {
    return 24.0f;
}

float IHM08M1_ReadTemperature(void) {
    return 25.0f;
}

float IHM08M1_ReadPotentiometer(void) {
    return 0.0f;
}

void IHM08M1_ReadRawCurrentSensors(uint16_t* raw_a, uint16_t* raw_b, uint16_t* raw_c) {
    if (raw_a) *raw_a = 2048;
    if (raw_b) *raw_b = 2048;
    if (raw_c) *raw_c = 2048;
}

void IHM08M1_CalibrateCurrentOffsets(uint16_t* offset_a, uint16_t* offset_b, uint16_t* offset_c) {
    if (offset_a) *offset_a = 2048;
    if (offset_b) *offset_b = 2048;
    if (offset_c) *offset_c = 2048;
}

#if (TARGET_POWER_STAGE == BOARD_TYPE_IHM08M1_L6398)
/* Route unified PowerStage_* calls to IHM08M1 implementation */
const PowerStageCapabilities_t* PowerStage_GetCapabilities(void) { return IHM08M1_GetCapabilities(); }
void PowerStage_Init(void) { IHM08M1_Init(); }
void PowerStage_SetPWM(uint16_t u, uint16_t v, uint16_t w) { IHM08M1_SetPWM(u, v, w); }
void PowerStage_StartPWM(void) { IHM08M1_StartPWM(); }
void PowerStage_StopPWM(void) { IHM08M1_StopPWM(); }
void PowerStage_EnablePhase(uint8_t m) { IHM08M1_EnablePhase(m); }
void PowerStage_DisableBridge(void) { IHM08M1_DisableBridge(); }
bool PowerStage_IsFaultActive(void) { return IHM08M1_IsFaultActive(); }
void PowerStage_ClearFault(void) { IHM08M1_ClearFault(); }
void PowerStage_SetCurrentTripThreshold(float a) { IHM08M1_SetCurrentTripThreshold(a); }
uint8_t PowerStage_ReadHallSensors(void) { return IHM08M1_ReadHallSensors(); }
void PowerStage_SetStatusLED(bool s) { IHM08M1_SetStatusLED(s); }
void PowerStage_ToggleStatusLED(void) { IHM08M1_ToggleStatusLED(); }
float PowerStage_ReadVBusVoltage(void) { return IHM08M1_ReadVBusVoltage(); }
float PowerStage_ReadTemperature(void) { return IHM08M1_ReadTemperature(); }
float PowerStage_ReadPotentiometer(void) { return IHM08M1_ReadPotentiometer(); }
void PowerStage_ReadRawCurrentSensors(uint16_t* a, uint16_t* b, uint16_t* c) { IHM08M1_ReadRawCurrentSensors(a, b, c); }
void PowerStage_CalibrateCurrentOffsets(uint16_t* a, uint16_t* b, uint16_t* c) { IHM08M1_CalibrateCurrentOffsets(a, b, c); }
void PowerStage_GetInjectedCurrents(float* ia, float* ib, float* ic) { if (ia) *ia = 0.0f; if (ib) *ib = 0.0f; if (ic) *ic = 0.0f; }
void PowerStage_OpticalResetEdgeState(void) {}
#endif
