# Worklog: BLDC Motor Stall Validation, Diagnostic Tooling & Test Automation

- **Date**: 2026-10-18
- **Time**: 16:10 MDT
- **Agent**: Antigravity Pair Programmer
- **User**: Dan Beattie

---

## 1. Summary of Actions
- **Developed Automated Test Suite (`tools/run_motor_test.py`)**:
  - Implemented end-to-end automation tool that inspects motor state, configures target profiles ('1'-'4'), adjusts speeds ('>', '<'), applies duty cycles ('[', ']'), zeros tachometer counter ('z'), and initiates/stops runs via SPACE.
  - Automatically captures synchronized ANSI telemetry, exports CSV time-series, and generates diagnostic evaluation of rotation vs stall.
- **Enhanced Telemetry Reconstructor (`tools/reconstruct_telemetry.py`)**:
  - Added full token parsing for Line 14 Optical Tachometer (`(14, 31)`) and Line 21 Three-Speed Comparator (`(21, 31)`: `Target | Est | Real RPM`).
  - Added optical metrics to CSV output (`real_rpm`, `optical_rpm`, `optical_revs`, `optical_status`) and summary statistics.
- **Fixed Button PC13 Startup Glitch (`src/main.c`)**:
  - Initialized `btn_prev = 0xFF` on boot to prevent spurious falling-edge detection on uninitialized GPIO input during MCU power-on reset.
- **Identified and Corrected Pole Pair Configuration (`src/motor_profiles.c`)**:
  - Discovered motor notes specifying **12N14P** (12 stator slots, 14 rotor magnets = 7 pole pairs).
  - Corrected `.pole_pairs` from 14 to 7 across profiles, fixing a $2\times$ over-frequency error in open-loop V/F drive.
  - Scaled base duty to 12.0% for 12V VBUS supply.
- **Implemented S-Curve Deceleration Taper (`src/motor_control.c`)**:
  - Added dynamic slew rate tapering within 40 RPM of target speed to prevent synchronous pullout caused by flywheel inertia overshoot upon arriving at steady state.
- **Resolved Oscilloscope Double-Speed Discrepancy (40 kHz TIM1 Center-Aligned UEV Root Cause)**:
  - User confirmed photodetector fixture strictly generates **1 pulse per revolution** (`OPTICAL_PULSES_PER_REV = 1`).
  - Discovered that `TIM1` was configured in Center-Aligned Mode 1 with `RepetitionCounter = 0`, generating Update Events (UEV) and interrupts at **both crest and trough (40 kHz)**.
  - The 20 kHz SPWM phase accumulator step was thus executed at 40 kHz, spinning the rotor at **2x commanded electrical speed** (commanded 350 RPM produced 700 RPM physical rotation and 11.7 Hz pulse on oscilloscope).
  - Configured `htim1.Init.RepetitionCounter = 1` in `src/ihm07m1.c` so UEV fires once per period (20 kHz) at underflow, achieving exact 1:1 speed alignment between console command, estimated speed, optical tachometer, and oscilloscope pulse frequency.
- **Fixed Console Differential Shadow Bug (`src/console.c`)**:
  - Added dedicated `line21_optical_rpm` shadow member to decouple Row 21 (`Target | Est | Real RPM`) from Row 14, ensuring Row 21 redraws when optical speed updates.
- **Enhanced Telemetry Reconstructor ANSI Tokenizer (`tools/reconstruct_telemetry.py`)**:
  - Sliced token text between consecutive cursor positioning escapes (`\x1b[<row>;<col>H`) rather than stopping at embedded color escapes (`\x1b[1;32m`), preserving status tags and rev counts.
- **Live Hardware Validation (Runs 9, 10 & 11)**:
  - **Run 9 (8-second test at 350 RPM)**: Motor smoothly accelerated to 350 RPM, counting 43 revs with peak current 1011 mA. Confirmed exact 1:1 alignment at 350 RPM.
  - **Run 10 (15-second test at 375 RPM)**: Motor turned 123 revs. Validated the user's observation: motor accelerates in synchronism at 375 RPM (6.25 Hz pulse, ~15-25 mA current), and when slip occurs, optical sensor detects rotor oscillating at the 7-pole stator electrical field frequency (~2500 RPM / 41.7 Hz) with reactive current surges up to 1.19 A.
  - **Run 11 (45-second Multi-Stage Speed Test: 300 -> 700 -> 1100 RPM)**:
    - Added `--speed-steps` and `--step-duration` support to `tools/run_motor_test.py`.
    - Executed sequential transitions: 300 RPM $\to$ 700 RPM $\to$ 1100 RPM (14s hold per plateau) at 12V supply.
    - Motor completed **603 revolutions** across the full run.
    - Successfully transitioned to 1100 RPM and locked in solid synchronism at **1100 RPM** (`Real: 1100 RPM`, 18.33 Hz optical pulse rate).
    - Current consumption measured across stages:
      - 300 RPM: 158 mA avg, 1302 mA peak transient, 22.7%–25.0% duty.
      - 700 RPM: 269 mA avg, 1473 mA peak transient, 26.1%–34.1% duty.
      - 1100 RPM: 297 mA avg, 1509 mA peak transient, 35.4%–43.2% duty.
    - Maximum peak current across the entire test was 1.51 A, safely below the 2.2 A compliance limit.
    - Confirmed roadmap toward Field-Oriented Control (FOC) to eliminate open-loop load angle oscillations.

---

## 2. Artifacts & Outputs
- **Tools**:
  - `tools/run_motor_test.py` (Fixed target RPM regex for Line 21, added optical_rpm fallback)
  - `tools/reconstruct_telemetry.py` (Fixed tokenization of ANSI-styled fields)
  - `tools/capture_console.py` (Updated keymap)
- **Firmware**:
  - `src/ihm07m1.c` (Set `RepetitionCounter = 1` on TIM1 for 20 kHz center-aligned UEV)
  - `src/motor_control.c` (Set `OPTICAL_PULSES_PER_REV = 1`, S-curve taper, V/F linkage)
  - `src/console.c` (Added `line21_optical_rpm` to `MainShadow_t`)
  - `src/motor_profiles.c` (12N14P 7 pole pairs correction, 12V duty scaling)
- **Documentation**:
  - `doc/features/09_motor_stall_validation_and_test_automation.md`
  - `doc/worklogs/2026-10-18_motor_stall_validation_and_test_automation.md`
  - `test_runs/*.log` and `test_runs/*.csv` (Captured empirical test data)
