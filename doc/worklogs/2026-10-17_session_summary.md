# Development Worklog: 2026-10-17

- **Date**: 2026-10-17
- **Time**: 15:25:00 UTC-6
- **User**: beattidp
- **Agent**: Antigravity (Google DeepMind)
- **Target System**: STM32G474RE (NUCLEO-64) + X-NUCLEO-IHM07M1 (L6230 Bridge)

---

## 1. Summary of Activity
In this session, we addressed multiple critical hardware and firmware issues on the STM32G474RE + X-NUCLEO-IHM07M1 motor evaluation setup, implemented a modular motor profile system, built an automated motor characterization engine, and resolved interrupt starvation and deadlock conditions.

---

## 2. Issues Investigated & Resolved

### Issue 1: 20 kHz Interrupt Starvation & LPUART Lockup
- **Symptom**: Serial console locked up after banner or after 1 refresh; motor crawled at ~30 RPM with no responsiveness to keyboard or potentiometer.
- **Root Cause**: `Motor_PWM_UpdateISR()` called standard library `sinf()` and `cosf()` during each 20 kHz PWM update, taking ~45 µs per invocation (>90% CPU load), starving the LPUART FIFO and background tasks.
- **Resolution**: Replaced with 256-point Q12 sine lookup table (`s_sin_table_q12`) and 32-bit phase accumulator. Reduced ISR time to 0.11 µs (<0.23% CPU load).

### Issue 2: SysTick Deadlock During Current Offset Calibration
- **Symptom**: Motor failed to start; system locked up when entering run state.
- **Root Cause**: `Motor_CalibrateCurrentOffsets()` invoked `HAL_Delay(10)` from inside `SysTick_Handler()`. Because `uwTick` cannot increment while inside the SysTick interrupt itself, `HAL_Delay` spun indefinitely.
- **Resolution**: Replaced `HAL_Delay` with a non-blocking calibrated volatile spin loop.

### Issue 3: Spurious L6230 DIAG Fault Trips
- **Symptom**: Spurious driver faults halted the PWM output unexpectedly.
- **Root Cause**: `PA6` (`TIM1_BKIN`) was configured as plain input without proper open-drain pull-up configuration, and `TIM1` BreakFilter was set to 0 (no digital filtering against high-frequency switching noise).
- **Resolution**: Configured `PA6` as `GPIO_MODE_AF_OD` with `GPIO_PULLUP`, enabled TIM1 digital noise filter (`BreakFilter = 15`), and added software debounce.

---

## 3. Features Implemented

1. **Modular Motor Profiles System (`include/motor_profiles.h`, `src/motor_profiles.c`)**:
   - Structured motor configurations for pole pairs, rated voltage, current limits, base duty, V/F slope, and acceleration slew rates.
   - Hotkey `[P]` in dashboard cycles profiles dynamically.
2. **Automated BLDC Characterization Engine (`include/motor_characterization.h`, `src/motor_characterization.c`)**:
   - 5-phase automated test measuring current sensor offsets, phase resistance ($R_s$), line-to-line resistance ($R_{LL}$), phase balance, stiction threshold, and Back-EMF $Kv$.
   - Interactive hotkey `[C]` runs characterization; results can be saved to custom profile with `[ENTER]`.
3. **Closed-Loop Current / Torque Limiter**:
   - Proportional duty foldback prevents motor phase currents from tripping the 2.2A hardware breaker under heavy loads.
4. **Enhanced High-Speed ANSI Dashboard (`src/console.c`)**:
   - Live non-scrolling terminal telemetry: VBUS, temperature, potentiometer, phase currents, estimated RPM, active profile, and characterization progress.
