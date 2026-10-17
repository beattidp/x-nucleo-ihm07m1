# Feature Specification: Unified Power Stage HAL & 24V Voltage Feedforward Engine

- **Status**: Implemented, Built & Uploaded to Hardware
- **Date**: 2026-10-17
- **Target Boards**:
  - Primary Active: **X-NUCLEO-IHM07M1** (L6230 Monolithic DMOS 3-Phase Bridge, 1.4A RMS / 2.8A peak)
  - Future Supported: **X-NUCLEO-IHM08M1** (L6398 High/Low-Side Gate Drivers + STL220N6F7 discrete MOSFETs, 15A continuous / 30A peak)
- **Source Modules**:
  - [`include/power_stage.h`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/include/power_stage.h)
  - [`include/driver_l6398_ihm08m1.h`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/include/driver_l6398_ihm08m1.h), [`src/driver_l6398_ihm08m1.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/driver_l6398_ihm08m1.c)
  - [`include/ihm07m1.h`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/include/ihm07m1.h), [`src/ihm07m1.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/ihm07m1.c)
  - [`src/motor_control.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_control.c)
  - [`src/motor_characterization.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_characterization.c)
  - [`src/motor_profiles.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_profiles.c)

---

## 1. Problem Statement & Motivation
Transitioning the testbench from 12V to **24V** (up to 6S / 25.2V) to characterize a low-resistance, high-torque **5010 360KV** flywheel motor presents two critical engineering challenges:
1. **Voltage-Duty Scaling & Overcurrent Hazard**:
   Doubling bus voltage doubles terminal volts per 1% PWM duty ($0.24\text{ V}$ at 24V vs $0.12\text{ V}$ at 12V). On low-resistance windings ($R_s \approx 0.3 \dots 0.8\ \Omega$), fixed open-loop pulses will double current draw and immediately trip the L6230's 2.2A hardware breaker.
2. **Hardware Coupling**:
   Firmware logic was tightly coupled to the L6230 on the X-NUCLEO-IHM07M1. The user plans to evaluate a larger 170KV motor requiring the higher-current **X-NUCLEO-IHM08M1** board with **L6398** gate drivers. The power stage interface must be cleanly abstracted.

---

## 2. Power Stage Hardware Abstraction Layer (HAL)

### 2.1 Capability Data Model
All board-specific voltage divider ratios, current sense gains, shunt counts, and ratings are queried through a constant descriptor:
```c
typedef struct {
    PowerStageBoardType_t board_type;
    const char*           board_name;
    const char*           driver_ic;
    float                 max_voltage_v;
    float                 nominal_voltage_v;
    float                 max_continuous_current_a;
    float                 peak_current_a;
    uint8_t               shunt_count;
    float                 shunt_resistance_ohm;
    float                 opamp_gain;
    float                 current_sense_gain;
    float                 vbus_divider_ratio;
    uint16_t              pwm_arr;
} PowerStageCapabilities_t;
```

### 2.2 Board Selection
Selection is controlled via `#define TARGET_POWER_STAGE`:
- `BOARD_TYPE_IHM07M1_L6230`: routes to L6230 single-ended PWM + enable pins on PC10-PC12 and PB4 current reference.
- `BOARD_TYPE_IHM08M1_L6398`: routes to L6398 complementary PWM with hardware deadtime insertion and discrete STL220N6F7 power MOSFETs.

---

## 3. Bus Voltage Feedforward Scaling

In `src/motor_control.c`, active duty cycle is dynamically normalized against instantaneous bus voltage $V_{\text{BUS}}$:
$$V_{\text{scale}} = \frac{V_{\text{rated}}}{V_{\text{BUS}}}$$
$$\text{Duty}_{\text{effective}} = \text{Duty}_{\text{commanded}} \times V_{\text{scale}}$$
- When bus voltage doubles from 12V to 24V, active duty automatically scales down by $0.5\times$.
- Minimum duty clamp scales dynamically to prevent over-excitation:
  $$\text{Duty}_{\text{min}} = 4.0\% \times \left(\frac{12.0\text{ V}}{V_{\text{BUS}}}\right) \quad (\approx 2.0\% \text{ at } 24\text{V})$$
- Motor stator terminal voltage remains constant and independent of supply voltage variations or battery discharge sag.

---

## 4. Voltage-Adaptive Motor Characterization

In `src/motor_characterization.c`:
- **Phase Resistance Pulses**: Rather than fixed 5% duty, pulses are calculated from a target terminal voltage $V_{\text{test}} = 0.60\text{ V}$:
  $$\text{Duty}_{\text{test}} = \frac{0.60\text{ V}}{V_{\text{BUS}}} \times 100\% \quad (\approx 2.5\% \text{ at } 24\text{V})$$
- **Rotor Alignment / Stiction**: Voltage ramp from $1.20\text{ V} \to 2.40\text{ V}$ ($5.0\% \to 10.0\%$ duty at 24V).
- **Dynamic Spin Tests**:
  - 50 Hz test: target $2.64\text{ V}$ stator voltage ($11.0\%$ duty at 24V).
  - 100 Hz test: target $3.36\text{ V}$ stator voltage ($14.0\%$ duty at 24V).
- **Recommended Profile Parameters**: Stator base duty and Back-EMF $Kv$ slope are voltage-normalized before saving into Profile 3 (`Custom / Characterized`).
