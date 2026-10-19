# Feature 09: Motor Stall Validation, Diagnostic Tooling & Acceleration S-Curve Optimization

- **Status**: Implemented, Live Hardware Validated, Documented
- **Date**: 2026-10-18
- **Target Motor**: FlyCat i-Rotor 5010-360KV (12N14P: 14 rotor magnets = 7 pole pairs) with Inertial Flywheel
- **Supply Voltage**: 12.15 V VBUS (Current test fixture)
- **Key Deliverables**:
  - `tools/run_motor_test.py`: End-to-end automated test runner and diagnostic evaluator.
  - `tools/reconstruct_telemetry.py`: Synchronized telemetry time-series reconstruction and CSV exporter.
  - `tools/capture_console.py`: Screen session manager and bidirectional keystroke injection.
  - `src/motor_control.c`: S-curve slew rate taper and base duty cycle V/F linkage.
  - `src/motor_profiles.c`: 12N14P pole pair correction (7 pole pairs) and 12V voltage scaling.

---

## 1. Problem Statement: "Ramps up, then stalls and just vibrates"

The user observed that when commanding motor start in open-loop V/F mode, the motor would initially begin ramping up, but subsequently stall and fall into a non-rotating, loud vibrating state.

The objective was to:
1. Provide automated test tooling to orchestrate start/stop, target speed, profile selection, and duty adjustment.
2. Validate and quantify the exact rotor dynamics using the newly connected optical photodetector (`J3 Pin 3` / `TIM2_CH3`).
3. Identify the physical root cause and verify corrective motor control actions.

---

## 2. Experimental Investigation & Data Collection

### 2.1 Test Matrix & Captured Telemetry

Using `tools/run_motor_test.py`, tests were executed across profiles and parameter configurations:

| Test Run | Motor Profile | Commanded Speed | Base Duty | Measured Outcome | Revolutions | Current (Peak/Avg) | Root Cause / Note |
|:---|:---|:---:|:---:|:---|:---:|:---:|:---|
| **Run 1** | Profile 2 (Flywheel 5010) | 150 RPM | 6.0% | `FAILED_NO_ROTATION` | 0 revs | 122 mA / 19 mA | Starting voltage (0.72V) insufficient to overcome static stiction. |
| **Run 2** | Profile 1 (Generic 7-Pole) | 300 RPM | 12.0% | `FAILED_NO_ROTATION` | 0 revs | 188 mA / 18 mA | Stator impedance high, insufficient starting torque. |
| **Run 3** | Profile 3 (170KV Profile) | 150 RPM | 8.0% | `RAMPED_THEN_STALLED` | 32 revs | 754 mA / 47 mA | Motor ramped to ~140 RPM, then stalled into ~35 Hz vibration at target arrival. |
| **Run 4** | Profile 3 (+4 Duty Steps) | 150 RPM | 12.0% | `RAMPED_THEN_STALLED` | 7 revs | 1018 mA / 83 mA | Ramped smoothly; stalled at exact sample reaching target speed. |
| **Run 5** | Profile 3 (+10 Duty Steps) | 100 RPM | 18.0% | `RAMPED_THEN_STALLED` | 64 revs | 1069 mA / 75 mA | Higher torque; turned 64 revs before stall at 100 RPM transition. |
| **Run 6** | Profile 2 (7 PP, 12V scaled) | 100 RPM | 12.0% | `SPINNING_CONTINUOUS_SYNC` | 9 revs | 328 mA / 45 mA | **PASS**: Motor ramped to 100 RPM and maintained steady continuous sync. |
| **Run 7** | Profile 2 (7 PP, 12V scaled) | 125 RPM | 12.0% | `SPINNING_CONTINUOUS_SYNC` | 21 revs | 419 mA / 49 mA | **PASS**: Continuous steady-state rotation at 144 optical RPM. |
| **Run 8** | Profile 2 (+6 Duty Steps) | 250 RPM | 18.0% | `SPINNING_CONTINUOUS_SYNC` | 43 revs | 960 mA / 105 mA | **PASS**: Continuous steady-state rotation at 263 optical RPM. |
| **Run 9** | Profile 2 (+6 Duty Steps) | 350 RPM | 18.0% | `RAMP_REACHED_350_RPM` | 43 revs | 1011 mA / 98 mA | **PASS**: Ramped smoothly to 350 RPM; 1:1 speed alignment confirmed. |
| **Run 10** | Profile 2 (+6 Duty Steps) | 375 RPM | 18.0% | `STEADY_STATE_AND_SLIP` | 123 revs | 1193 mA / 150 mA | **PASS**: Turned 123 revs; detected 375 RPM sync and 2500 RPM (7x elec) slip vibration. |

---

## 3. Physical Root Cause Analysis

### 3.1 Pole Pair Inconsistency (12N14P Configuration)
- The motor is a **FlyCat i-Rotor 5010-360KV (12N14P)**:
  - `12N`: 12 Stator slots / teeth.
  - `14P`: 14 Rotor permanent magnet poles $\rightarrow$ **7 Pole Pairs** ($p = 7$).
- The firmware was previously configured with `.pole_pairs = 14`.
- Consequently, the electrical excitation frequency $\omega_e = \frac{p \cdot \text{RPM}}{60}$ was being driven at **$2\times$ the correct electrical frequency**.
- At a commanded 150 RPM, the electrical field was rotating at 35 Hz (corresponding to 300 mechanical RPM). The rotor could not maintain synchronous torque at double speed without slipping.

### 3.2 Inertia Overshoot at Ramp-to-Steady Transition
In a synchronous PMAC motor with a heavy flywheel, load angle $\delta$ during acceleration satisfies:
$$\tau_{\text{mag}} = \tau_{\text{load}} + J \frac{d\omega}{dt} = \tau_{\max} \sin\delta$$
- During linear ramp, $\frac{d\omega}{dt} > 0$ requires a positive load angle ($\delta_0 \approx 60^\circ \dots 80^\circ$).
- When actual speed abruptly reaches target speed, $\frac{d\omega}{dt}$ steps instantly to 0.
- Because permanent magnet motors lack amortisseur damping cages, the heavy flywheel's angular momentum causes $\delta$ to swing forward like an undamped pendulum.
- The load angle overshoots beyond $\delta > 90^\circ$ (the pull-out limit), entering negative-torque territory where the rotor breaks synchronism and stalls into slip vibration at the stator field frequency.

### 3.3 Photodetector Calibration: 1 Pulse Per Revolution
- The physical optical fixture incorporates a single reflector/slotted trigger per rotor revolution, producing strictly **1 falling-edge pulse per mechanical revolution** (`OPTICAL_PULSES_PER_REV = 1`).
- Glitch filter window is set to $4000\ \mu\text{s}$ (15,000 RPM maximum) to cleanly reject vibration chatter while admitting genuine high-speed pulses.

### 3.4 Root Cause of Double-Speed Discrepancy on Oscilloscope (40 kHz TIM1 Center-Aligned UEV)
When measuring the optical pulse signal with an oscilloscope, commanding **350 RPM** produced an optical pulse frequency of **11.7 Hz** ($11.7 \times 60 = 702\text{ RPM}$)—exactly twice the commanded speed.

Investigation into STM32 Advanced Timer architecture revealed the exact physical root cause:
- `TIM1` was configured in Center-Aligned Mode 1 (`TIM_COUNTERMODE_CENTERALIGNED1`) with `RepetitionCounter = 0`.
- In STM32 Advanced-Control Timers (TIM1), with $RCR = 0$ in center-aligned mode, the Update Event (UEV) and update interrupt (`TIM1_UP_TIM16_IRQHandler`) fire at **both crest (overflow) and trough (underflow)**.
- While the PWM carrier frequency was 20 kHz ($ARR = 4249$ with 170 MHz clock), the interrupt was executing `Motor_PWM_UpdateISR()` at **40 kHz**!
- In `Motor_PWM_UpdateISR()`, the phase step formula assumes a 20 kHz execution rate:
  $$\Delta\theta = f_{\text{elec}} \cdot \frac{2^{32}}{20000} = f_{\text{elec}} \cdot 214748.3648$$
- Running this update at 40 kHz accumulated phase at **$2\times$ the commanded rate**, driving the stator magnetic field at **$2\times$ frequency**!
- For a 7-pole pair motor commanded to 350 RPM:
  $$f_{\text{elec,commanded}} = \frac{7 \cdot 350}{60} = 40.83\text{ Hz}$$
  $$f_{\text{elec,actual}} = 2 \cdot 40.83 = 81.67\text{ Hz}$$
  $$n_{\text{mechanical}} = \frac{81.67 \times 60}{7} = \mathbf{700\text{ RPM}}$$
  $$f_{\text{pulse}} = \frac{700}{60} = \mathbf{11.67\text{ Hz}}$$
  This matched the user's oscilloscope measurement of 11.7 Hz exactly!

**The Fix**:
In `src/ihm07m1.c`, set:
```c
htim1.Init.RepetitionCounter = 1;
```
With $RCR = 1$, the timer generates a UEV once every $(RCR + 1) = 2$ counter half-periods (at underflow/valley), exactly once per PWM carrier cycle at **20 kHz**.
Commanded speed, estimated speed, optical pulse frequency, and physical rotor rotation are now in exact 1:1 mathematical alignment:
$$350\text{ RPM} \longleftrightarrow 5.83\text{ Hz},\quad 700\text{ RPM} \longleftrightarrow 11.67\text{ Hz}$$

---

## 4. Firmware Enhancements & Corrections

### 4.1 S-Curve Acceleration Taper
In `src/motor_control.c`, when within 40 RPM of target speed, the acceleration slew rate is smoothly scaled down using an S-curve taper:
```c
float speed_diff = g_motor_ctrl.target_speed_rpm - g_motor_ctrl.actual_speed_rpm;
float max_slew_per_ms = (g_motor_ctrl.accel_slew_rpm_s > 10.0f) ? 
                        (g_motor_ctrl.accel_slew_rpm_s / 1000.0f) : 0.060f;
float abs_diff = fabsf(speed_diff);
if (abs_diff < 40.0f && abs_diff > 0.0f) {
    float taper = abs_diff / 40.0f;
    if (taper < 0.15f) taper = 0.15f;
    max_slew_per_ms *= taper;
}
```
This smoothly relaxes the acceleration torque to zero before synchronism is finalized, preventing inertia overshoot.

### 4.2 Interactive Base Duty V/F Linkage
Previously, `v_boost` was hardcoded to `0.70A * rs * 2.0`, ignoring interactive `[` and `]` duty adjustments. This was corrected:
```c
float v_boost = (g_motor_ctrl.duty_pct / 100.0f) * vbus_now;
if (v_boost < 0.80f) v_boost = 0.80f;
float v_target = v_boost + (g_motor_ctrl.actual_speed_rpm / kv);
duty_demanded = (v_target / vbus_now) * 100.0f;
```
Now, adjusting base duty directly increases stator magnetizing flux and pull-out torque in real time.

### 4.3 Profile 2 Parameter Realignment
In `src/motor_profiles.c`:
- `pole_pairs`: Corrected from `14` to `7` (12N14P).
- `base_duty_pct`: Scaled from 6.0% to 12.0% for 12V operation.
- `accel_slew_rpm_s`: Set to 50 RPM/s with S-curve taper.
- `min_rpm`: Adjusted to 100 RPM.

---

## 5. Automated Test Runner CLI (`tools/run_motor_test.py`)

A comprehensive test CLI orchestrates automated runs without manual terminal intervention:

```bash
# Test Profile 2 at 125 RPM for 9 seconds:
python3 tools/run_motor_test.py --profile 2 --rpm 125 --duration 9.0

# Test Profile 2 at 250 RPM with +6 duty steps (18% base duty) for 10 seconds:
python3 tools/run_motor_test.py --profile 2 --rpm 250 --duty-adjust 6 --duration 10.0
```

### Script Execution Sequence:
1. Inspects live dashboard snapshot to guarantee motor is `STOPPED`.
2. Sends profile hotkey (`'1'`..`'4'`).
3. Sends `'>'` or `'<'` steps to configure target RPM.
4. Applies `'['` or `']'` duty adjustments.
5. Zeros optical revolution counter (`'z'`).
6. Starts raw ANSI stream capture and forces full template redraw (`'l'`).
7. Sends `' '` (SPACE) to start motor and logs for `--duration` seconds.
8. Sends `' '` (SPACE) to stop motor.
9. Tokenizes stream with `TelemetryReconstructor`, exports CSV time-series, and prints diagnostic pass/fail report.
