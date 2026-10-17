# Feature Specification: Automated BLDC Motor Characterization Engine

- **Status**: Implemented & Verified
- **Date**: 2026-10-17
- **Source Modules**: [`include/motor_characterization.h`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/include/motor_characterization.h), [`src/motor_characterization.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_characterization.c)

---

## 1. Overview
Evaluating unknown or uncalibrated brushless motors often requires external lab instruments to determine winding resistance, phase balance, stiction torque, and velocity constant ($Kv$). 

The **Motor Characterization Engine** provides an automated, on-board 5-phase test routine that non-destructively evaluates the attached motor using the X-NUCLEO-IHM07M1's onboard 3-shunt current sense amplifiers (TSV994) and voltage telemetry.

---

## 2. Characterization Workflow

```
[Start [C]]
    │
    ▼
[Phase 1: Current Sensor Zero-Offset Calibration] (100 samples per phase, motor idle)
    │
    ▼
[Phase 2: Phase Resistance & Balance Measurement] (Controlled DC pulses across U-V, V-W, W-U)
    │
    ▼
[Phase 3: Rotor Alignment & Stiction Threshold] (Gradual DC flux ramp 15% -> 28%)
    │
    ▼
[Phase 4: Dynamic Spin & Back-EMF Kv Estimation] (50 Hz and 100 Hz electrical frequency tests)
    │
    ▼
[Phase 5: Diagnostic Report & Optional Profile Save] (Displays full report, [ENTER] to save)
```

---

## 3. Mathematical Foundations

### 3.1 Phase Resistance ($R_s$) & Line-to-Line Resistance ($R_{LL}$)
A short, low-duty pulse is applied across each phase pair while keeping the third phase floating:
$$R_{UV} = \frac{V_{\text{terminal}, UV}}{I_{UV}}, \quad R_{VW} = \frac{V_{\text{terminal}, VW}}{I_{VW}}, \quad R_{WU} = \frac{V_{\text{terminal}, WU}}{I_{WU}}$$
$$R_{LL} = \frac{R_{UV} + R_{VW} + R_{WU}}{3}, \qquad R_s = \frac{R_{LL}}{2}$$

Phase balance is computed to verify symmetric winding health:
$$\text{Phase Balance} = \left(1.0 - \frac{\max(|R - R_{LL}|)}{R_{LL}}\right) \times 100\%$$

### 3.2 Dynamic Back-EMF & $Kv$ Estimation
During constant-frequency rotation at steady electrical frequency $f_{\text{elec}}$:
$$\text{RPM} = \frac{f_{\text{elec}} \times 60}{\text{Pole Pairs}}$$
$$V_{\text{BEMF}} = V_{\text{applied}} - (I_{\text{phase}} \times R_s)$$
$$Kv = \frac{\text{RPM}}{V_{\text{BEMF}}} \quad [\text{RPM / V}]$$

---

## 4. Hardware Verification Results
Tested on target STM32G474RE + X-NUCLEO-IHM07M1:
- Zero-offset calibration: $I_A = 2052\text{ LSB}$, $I_B = 2049\text{ LSB}$, $I_C = 2051\text{ LSB}$
- Measured phase resistance: $R_s = 5.965\ \Omega$ ($R_{LL} = 11.93\ \Omega$)
- Phase balance: **99.6%**
- Stiction threshold: **18.2% duty** ($I_{\text{stiction}} = 385\text{ mA}$)
- Measured $Kv$: **$575\text{ RPM/V}$**
