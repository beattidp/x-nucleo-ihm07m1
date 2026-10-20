# Worklog: Rapid Acceleration Tuning & 4000 RPM Bidirectional Deceleration Ladder Validation

**Date**: 2026-10-19  
**Time**: 23:55 MDT  
**Agent**: Antigravity  
**User**: beattidp  

---

## 1. Summary of Actions & Objectives
Following up on the user request to *"Activate the current regulators, then evaluate a more rapid acceleration. Also, I don't recall that deceleration was fully tested, as several tests failed at higher speeds."*:
1. **Evaluated rapid acceleration** ($500\text{ RPM/s}$ and $1000\text{ RPM/s}$) tailored to the low-inertia bare rotor (`5010-360KV`, 7 PP, no flywheel).
2. **Implemented interactive slew rate control** (`A<slew>\r`), live dashboard display on Row 22, and test runner CLI integration (`--slew`).
3. **Discovered and analyzed the 3896.1 RPM ($N = 44.0$) carrier integer harmonic resonance trip**, resolving it permanently via **DC-decoupled AC perturbation active damping**.
4. **Executed and passed the 4000 RPM bidirectional deceleration ladder** ($864 \to 1728 \to 2880 \to 4000 \to 2880 \to 1728 \to 864 \to 288\text{ RPM}$) with zero regenerative trips, zero slips, and 1,277 counted revolutions.

---

## 2. Test Runs & Results

### 2.1 Rapid Acceleration Runs
1. **500 RPM/s to 3600 RPM (`run_20261019_233810.csv`)**:
   - 0 to 3600 RPM ramp time: **7.2 seconds**.
   - Result: `[PASS] MOTOR ROTATING IN SYNCHRONISM` (724 revolutions).
   - Peak phase current: Max 23 mA, Avg 12.7 mA.
2. **1000 RPM/s to 3600 RPM (`run_20261019_233836.csv`)**:
   - 0 to 3600 RPM ramp time: **3.6 seconds**.
   - Result: `[PASS] MOTOR ROTATING IN SYNCHRONISM` (587 revolutions).
   - Peak phase current: Max 27 mA, Avg 12.5 mA.

### 2.2 Harmonic Boundary Investigation (3750 – 3920 RPM)
- Prior open-loop tests and initial runs tripped at ~3890–3896 RPM.
- Analytical calculation identified $f_{\text{PWM}} / f_e = 20,000 / 454.545 = 44.000$ (exact integer ratio). The stationary 3-phase sampling asymmetry created a false DC error in $I_d$, which previously saturated the active damping term against its clamp.
- Implemented $6.4\text{ Hz}$ DC tracking filter ($s\_id\_dc$), isolating $I_{d,\text{AC}}$ for damping.
- Stepped across the boundary:
  - 3750 RPM: 616 revs, 27 mA max -> **PASS** (`run_20261019_234939`)
  - 3800 RPM: 505 revs, 28 mA max -> **PASS** (`run_20261019_235007`)
  - 3850 RPM: 630 revs, 25 mA max -> **PASS** (`run_20261019_235030`)
  - 3880 RPM: 633 revs, 28 mA max -> **PASS** (`run_20261019_235054`)
  - 3920 RPM: 634 revs, 28 mA max -> **PASS** (`run_20261019_235120`)

### 2.3 Full 4000 RPM Bidirectional Deceleration Ladder (`run_20261019_235252.csv`)
- **Sequence**: $864 \to 1728 \to 2880 \to 4000 \to 2880 \to 1728 \to 864 \to 288\text{ RPM}$ (5.0s holds).
- **Result**: **[PASS] MOTOR ROTATING IN SYNCHRONISM across all 8 stages!**
- **Revolutions**: **1,277 revolutions** tracked by optical tachometer.
- **Max Optical Speed**: **4000 RPM**.
- **Phase Current**: Max **28 mA**, Avg **13.7 mA**.
- **Deceleration**: Smooth, controlled deceleration across all 4 down-steps with zero power supply trips.

---

## 3. Files Modified
- `include/motor_control.h`: Added `Motor_SetSlewRates` declaration.
- `src/motor_control.c`: Implemented `Motor_SetSlewRates`, increased speed clamp to 6500 RPM, added DC-decoupled AC perturbation active damping (`s_id_dc`).
- `src/main.c`: Added `A<slew>\r` interactive command parsing.
- `src/console.c`: Displayed active slew rate on Row 22, updated key legend.
- `tools/run_motor_test.py`: Added `--slew` CLI argument.
- `doc/features/13_rapid_acceleration_and_4000rpm_harmonic_decoupling.md`: Detailed feature documentation.
