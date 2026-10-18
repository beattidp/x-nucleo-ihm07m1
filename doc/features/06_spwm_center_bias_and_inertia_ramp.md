# Feature Specification: Center-Biased Bipolar SPWM & Flywheel Inertia Acceleration Engine

- **Status**: Implemented, Verified via Assembly Audit, Flashed to Target Hardware
- **Date**: 2026-10-17
- **Target Motor**: 5010 360KV High-Torque Sensorless BLDC (14 pole pairs / 28 poles) with Flywheel
- **Supply Voltage**: 24.0 V
- **Source Modules**:
  - [`src/motor_control.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_control.c)
  - [`include/motor_control.h`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/include/motor_control.h)
  - [`src/motor_characterization.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_characterization.c)
  - [`src/motor_profiles.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_profiles.c)

---

## 1. Problem Statement & Root Cause Analysis

When evaluating the 5010 360KV flywheel motor on the 24V supply:
1. **Characterization Vibration**: The motor vibrated loudly at 50 Hz and 100 Hz without spinning during the automated characterization routine.
2. **Pull-Out Stall**: In normal open-loop drive (Profile 1 or saved Profile 3), the motor began spinning up but stalled at ~800 RPM with persistent vibration.

Root cause analysis revealed four compounding physical issues:
- **Ground-Short Dynamic Braking**: The SPWM compare registers were modulated unipolar from $0 \dots \text{scale}$. At low duties (e.g. 10%), for 90% of every PWM cycle all three low-side DMOS switches were active, dead-shorting the windings to ground and generating intense rheostatic braking torque ($\tau_{\text{brake}} \propto \text{RPM}$) against the Back-EMF.
- **Inertia Pull-Out Margin**: 14 pole pairs provides only $6.4^\circ$ mechanical angle before load angle exceeds $90^\circ$ electrical (pullout). Characterization jumped 2 Hz $\to$ 50 Hz in 0.5s ($411\text{ RPM/s}$), causing immediate pullout slip on a heavy flywheel.
- **Voltage Starvation vs Back-EMF**: At 1000 RPM, $E_{\text{BEMF}} = 2.78\text{ V}$, but commanded stator voltage was $<1.5\text{ V}$.
- **Premature Foldback Chatter**: Software torque limiter folded back at 75% limit (0.90A), slashing acceleration torque and inducing hunting.

---

## 2. Technical Architecture & Implementation

### 2.1 Center-Biased Bipolar SPWM Modulation
The unipolar modulation table was replaced with a signed 256-point Q11 table:
$$\text{Table}[i] = \text{round}\left(2047.0 \times \sin\left(\frac{2\pi \cdot i}{256}\right)\right) \in [-2047, +2047]$$

In `Motor_PWM_UpdateISR()`:
$$CCR_u = \frac{ARR}{2} + \left( \frac{\text{Table}[u\_idx] \times \text{scale}}{2048} \right)$$
$$CCR_v = \frac{ARR}{2} + \left( \frac{\text{Table}[v\_idx] \times \text{scale}}{2048} \right)$$
$$CCR_w = \frac{ARR}{2} + \left( \frac{\text{Table}[w\_idx] \times \text{scale}}{2048} \right)$$
- At any modulation depth, the average phase duty is strictly **50.0%**.
- The zero-vectors $V_0 [0,0,0]$ and $V_7 [1,1,1]$ are completely balanced, eliminating DC braking current.
- Inner loop requires only single-cycle multiply-shift-accumulate (`muls` and `add.w ... asr #11`), completing in $<0.11\ \mu\text{s}$ ($0.21\%$ CPU load at 20 kHz).

### 2.2 Inertia-Aware Acceleration Engine
- **DC Holding Alignment**: 1000 ms at 0 Hz ($V_{\text{align}} = 1.20\text{ V}$) to allow rotor alignment and flywheel pendulum swing to damp out completely.
- **Controlled Slew Ramp**: Motor starts at 1.0 Hz electrical (~4.3 RPM) and slews speed linearly at `accel_slew_rpm_s` (60.0 RPM/s for Profile 1), preventing load angle $\delta$ from ever exceeding $90^\circ$ electrical ($6.4^\circ$ mechanical).

### 2.3 Exact Back-EMF Feedforward V/F Law
Commanded stator terminal voltage tracks motor physics directly:
$$V_{\text{target}}(\text{RPM}) = V_{\text{boost}} + \frac{\text{RPM}}{Kv}$$
$$\text{Duty}_{\text{demanded}} = \frac{V_{\text{target}}}{V_{\text{BUS}}} \times 100\%$$
Where $V_{\text{boost}} = I_{\text{target}} \times 2 R_s \approx 1.0\text{ V} \dots 1.5\text{ V}$.
- At 300 RPM: $V_{\text{target}} \approx 1.83\text{ V}$ (7.6% duty at 24V).
- At 1000 RPM: $V_{\text{target}} \approx 3.78\text{ V}$ (15.7% duty at 24V).
- At 3000 RPM: $V_{\text{target}} \approx 9.33\text{ V}$ (38.9% duty at 24V).
Motor stator current and torque capability remain constant across the entire operating speed range.

### 2.4 Overhauled Characterization Spin Sequence
- **Step 3 (Rotor Alignment)**: 1200 ms with gentle 400 ms voltage ramp to 1.6V, then 800 ms hold.
- **Step 4a (Point 1)**: 25 Hz electrical (~107 RPM), 2500 ms ramp (~43 RPM/s), 1500 ms steady measurement.
- **Step 4b (Point 2)**: 50 Hz electrical (~214 RPM), 2500 ms ramp (~43 RPM/s), 1500 ms steady measurement.
- **Step 5 (Parameter Fit)**: Computes true rotating Back-EMF $Kv \approx 360$ and voltage recommendations.

---

## 3. Verification Results

1. **Static Analysis & Assembly Inspection**:
   - Disassembly of `Motor_PWM_UpdateISR()`: Verified pure integer instructions (`muls`, `add.w ... asr #11`, `str`).
   - Zero soft-float calls, execution time ~18 cycles (<0.11 µs).
2. **Flash & Memory**:
   - Flash: 40,664 bytes (7.8% of 512 KB).
   - RAM: 5,968 bytes (4.6% of 128 KB).
3. **Hardware Upload**:
   - Flashed via ST-Link on `/dev/ttyACM0` with verified verify OK.
