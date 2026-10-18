# Development Worklog: Resolution of SPWM Dynamic Braking Bug & Flywheel Inertia Pull-Out Stalls

- **Date**: 2026-10-17
- **Time**: 18:15:00 UTC-6
- **User**: beattidp
- **Agent**: Antigravity (Google DeepMind)
- **Target Hardware**: STM32G474RE (NUCLEO-64) + X-NUCLEO-IHM07M1 (L6230 Bridge)
- **Target Motor**: 5010 360KV High-Torque Sensorless BLDC (14 Pole Pairs / 28 Poles) for Flywheel Balancing Project
- **Supply Voltage**: 24.0 V

---

## 1. Summary of Changes

In response to the user's report that the 5010 360KV flywheel motor vibrated in place during characterization and stalled into vibration at ~800 RPM during open-loop running, this iteration:
1. Replaced unipolar 0-biased SPWM modulation with center-biased bipolar sinusoidal PWM (`s_sin_table_s12` and `half_arr`), eliminating the 90% ground-short dynamic braking condition.
2. Replaced fixed startup jump with inertia-aware, slew-rate-governed linear acceleration (60 RPM/s) starting from 1.0 Hz electrical (~4.3 RPM).
3. Replaced arbitrary V/F percentage slope with exact physical Back-EMF feedforward law ($V_{\text{target}} = V_{\text{boost}} + \text{RPM} / Kv$).
4. Overhauled the automated characterization routine with gentle 43 RPM/s ramps to 25 Hz and 50 Hz electrical, allowing high-inertia flywheels to reach steady state before sampling Back-EMF.
5. Replaced aggressive 75% current foldback with soft current regulation above 1.20A continuous ceiling.
6. Refined Profile 1 (`Flywheel 5010-360KV`) with `base_duty_pct = 6.0%`, `vf_slope_pct = 38.0%`, `accel_slew_rpm_s = 60.0f`, `min_rpm = 150.0f`, and `max_rpm = 4000.0f`.

---

## 2. Technical Details & Verification

- **Assembly Verification**: Disassembly of `Motor_PWM_UpdateISR()` verified pure 32-bit integer instructions (`muls`, `add.w ... asr #11`, `str`). Execution time is ~18 cycles (<0.11 µs), or 0.21% CPU load at 20 kHz.
- **PlatformIO Build**: 40,664 bytes Flash (7.8%), 5,968 bytes RAM (4.6%), zero warnings, zero errors.
- **Hardware Flashed**: Programmed and target reset via ST-Link on `/dev/ttyACM0`.
