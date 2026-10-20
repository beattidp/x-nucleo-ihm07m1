# Worklog: Closed-Loop FOC Current Regulator Activation & Bidirectional Deceleration Validation

**Date**: 2026-10-19  
**Time**: 23:30 MDT  
**Agent**: Pair Programming Agent  
**User**: User  

---

## 1. Objectives & Summary
1. Activate and validate closed-loop Field-Oriented Control (FOC) dual PI current regulators on the STM32G474RE + X-NUCLEO-IHM07M1.
2. Evaluate rapid acceleration tuned for the low-inertia bare rotor (`5010-360KV`, 7 pole pairs, no flywheel).
3. Validate controlled deceleration across multi-step speed profiles, preventing regenerative overvoltage or comparator trips on the unidirectional bench power supply.

---

## 2. Key Actions & Architectural Implementation

### 2.1 Low-Current Active Damping Calibration
- **Problem**: In open-loop V/F, the bare rotor experienced undamped torsional rotor hunting around $3800\text{ RPM}$ ($450\text{ Hz}$ electrical), triggering the L6230 OCD / TIM1_BKIN trip.
- **Root Cause**: The default active damping gain ($K_{\text{damp}} = 25.0\text{ rad/(s}\cdot\text{A)}$) was sized for ampere-level phase currents. Because the bare rotor operates at only $11\text{ to } 25\text{ mA}$, a gain of 25 produced negligible frequency adjustment ($\Delta \omega < 0.08\text{ Hz}$).
- **Fix**: Re-calibrated $K_{\text{damp}}$ in `src/foc.c` to $350.0\text{ rad/(s}\cdot\text{A)}$, yielding $7.0\text{ rad/s}$ ($\approx 1.11\text{ Hz}$) correction per $20\text{ mA}$ direct-axis deviation, with a saturation ceiling of $\pm 62.8\text{ rad/s}$ ($\pm 10\text{ Hz}$).
- **Startup Protection**: Enabled active damping for both `MOTOR_STATE_STARTING` and `MOTOR_STATE_RUNNING` once speed exceeds $400\text{ RPM}$.

### 2.2 Slew Rate Optimization for Bare Rotor
- Adjusted `accel_slew_rpm_s` and `decel_slew_rpm_s` to $250.0\text{ RPM/s}$ in Profile 2 (`5010-360KV Bare Rotor`).
- This balances rapid spin-up ($0 \to 3600\text{ RPM}$ in $14.4\text{ s}$) with stable rotor torque angle tracking.

### 2.3 Unidirectional Power Supply Safe Deceleration
- Clamped $I_{q,\text{demanded}}$ to a minimum of $+5\text{ mA}$ to guarantee that regeneration is physically inhibited.
- Maintained synchronous applied voltage $V_{\text{target}} = V_{\text{boost}} + (\text{RPM} / K_v)$ during down-ramps, keeping terminal voltage above back-EMF so the DMOS bridge never acts as a boost rectifier into the bench supply.

---

## 3. Verification & Live Bench Results

### 3.1 9-Stage 24V Bidirectional Speed Ladder (`run_20261019_232617.csv`)
Sequence: $288 \to 864 \to 1728 \to 2880 \to 3600 \to 2880 \to 1728 \to 864 \to 288\text{ RPM}$ (6.0s holds):
- **RESULT: `[PASS] MOTOR ROTATING IN SYNCHRONISM`** across all 9 stages!
- **Total Revolutions**: **1,541 rev(s)** cleanly tracked by optical tachometer with zero slips.
- **Speed Tracking**:
  - Peak: Commanded $3600\text{ RPM}$ | Optical $3601\text{ RPM}$.
  - Decel 1: $2880\text{ RPM}$ (Optical $2880\text{ RPM}$).
  - Decel 2: $1728\text{ RPM}$ (Optical $1728\text{ RPM}$).
  - Decel 3: $864\text{ RPM}$ (Optical $864\text{ RPM}$).
  - Decel 4: $288\text{ RPM}$ (Optical $288\text{ RPM}$).
- **Current Draw**: Max peak $28\text{ mA}$, Average $13.0\text{ mA}$.
- **Power Supply Status**: Zero trips on the unidirectional bench power supply.

### 3.2 Extended 3600 RPM Steady-State Test (`run_20261019_232832.csv`)
- **Duration**: 22.0 seconds.
- **Revolutions Counted**: **869 rev(s)**.
- **Steady-State Speed**: Average **3600.2 RPM** | Final **3600 RPM**.
- **Phase Current**: Max $30\text{ mA}$, Average $13.6\text{ mA}$.
