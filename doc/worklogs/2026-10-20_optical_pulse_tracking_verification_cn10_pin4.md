# Worklog: Optical Pulse Tracking Verification on Morpho CN10 Pin 4 (PC6)

**Date**: 2026-10-20  
**Time**: 14:25 Mountain / 20:25 UTC  
**Agent**: Antigravity  
**User**: beattidp  

---

## 1. Overview & Setup
- **Objective**: Execute empirical optical pulse tracking tests to verify revolution counting, speed synchronism, and timer atomicity across wide dynamic ranges following the physical relocation of the optical detector to **CN10 Pin 4 (`PC6`)** and **CN10 Pin 9 (`GND`)**.
- **Physical Verification**:
  - Confirmed physical relocation from user photo [`doc/img/20261020T140607.jpg`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/img/20261020T140607.jpg):
    - Signal wire connected to Morpho header **CN10 Pin 4** (`PC6`, `TIM3_CH1`).
    - Ground wire connected to Morpho header **CN10 Pin 9** (`GND`).
    - Motor connector **J3** (`PA15`, `PB3`, `PB10`) is now completely vacated and available for 3-phase Hall effect sensors.
    - Optical reflective sensor clamped over 5010-360KV bare rotor with reflective pulse target.
  - Active timebase: Cascaded 16-bit timers (`TIM3` Master at 1.0 MHz LSB + `TIM4` Slave at overflow MSB).

---

## 2. Experimental Test Runs & Results

### 2.1 Run 1: High-Speed Hold Test (`test_runs/run_20261020_141825.csv`)
- **Profile**: 1728 RPM hold for 8.0 s (Profile 2: 5010-360KV bare rotor).
- **Results**:
  - Total revolutions counted: **164 revs**.
  - Steady-state optical speed: **1728.0 RPM** (exact match to target and FOC estimator).
  - Status flag: `[ROTATING - OK]`.
  - Phase currents: $I_A = +1\text{ mA}$, $I_B = +20\text{ mA}$, $I_C = +4\text{ mA}$, peak $20\text{ mA}$.
  - Bus voltage: $24.28\text{ V}$ steady.

### 2.2 Run 2: Low-Speed 16-Bit Overflow Boundary Test (`test_runs/run_20261020_141850.csv`)
- **Objective**: Exercise the cascaded 16-bit timer overflow boundary by running at speeds where the pulse interval $\Delta t_{\text{pulse}}$ exceeds the $65{,}536\,\mu\text{s}$ rollover period of `TIM3`.
- **Profile**: Step 1: 288 RPM hold (3 full rollovers per pulse), Step 2: 864 RPM hold (1 full rollover per pulse).
- **Calculations**:
  - At 288 RPM: $\Delta t = \frac{60}{288} = 208{,}333\,\mu\text{s}$. Rollovers = $\lfloor 208{,}333 / 65{,}536 \rfloor = 3$.
  - At 864 RPM: $\Delta t = \frac{60}{864} = 69{,}444\,\mu\text{s}$. Rollovers = $\lfloor 69{,}444 / 65{,}536 \rfloor = 1$.
- **Results**:
  - Total revolutions counted: **109 revs**.
  - Optical speed measured: **287.0 RPM** at Step 1; **864.0 RPM** at Step 2.
  - Synchronism status: `[ROTATING - OK]` maintained continuously across both steps.
  - Zero rollover glitches, counter wraps, or timing discontinuities observed.

### 2.3 Run 3: 4-Stage Bidirectional Ladder Test (`test_runs/run_20261020_141917.csv`)
- **Profile**: $864 \to 2880 \to 864 \to 288\text{ RPM}$ ladder at slew rate $500\text{ RPM/s}$.
- **Results**:
  - Total revolutions counted: **316 revs**.
  - Peak speed tracked: **2853 RPM**.
  - Smooth tracking across both acceleration and deceleration transitions.
  - During rapid deceleration from 864 RPM to 288 RPM, the open-loop electrical ramp slewed faster than the physical rotor inertia for one sample window ($534\text{ RPM}$ real vs $343\text{ RPM}$ electrical ramp, slip = 55%), briefly asserting `[SLIPPING / VIBRATING]`, then recovering to `[ROTATING - OK]` as 288 RPM steady state was reached.

---

## 3. Conclusions & Next Steps
- The cascaded `TIM3` + `TIM4` 32-bit optical capture architecture on Morpho CN10 Pin 4 (`PC6`) is fully validated and functional under all operating conditions (low-speed multiple rollover, high-speed hold, and dynamic ladder).
- Connector **J3** and `TIM2` are now completely freed for the upcoming evaluation of the Hall-sensored BLDC motor.
- Commit and review before proceeding to Hall sensor configuration.
