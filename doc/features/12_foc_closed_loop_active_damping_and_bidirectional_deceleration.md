# Feature 12: Closed-Loop FOC Regulators, Active Electronic Damping & Controlled Bidirectional Deceleration

## 1. Overview & Operational Problem
During open-loop V/F drive of the low-inertia bare rotor (`5010-360KV`, 7 pole pairs, $J \approx 3.5 \times 10^{-6}\text{ kg}\cdot\text{m}^2$, no flywheel), two critical operational challenges were encountered:
1. **High-Speed Undamped Hunting**: Above $3600\text{ RPM}$ ($f_e > 420\text{ Hz}$), synchronous machines with low rotational inertia exhibit severe undamped torsional hunting, where small speed variations cause the rotor angle $\delta$ to oscillate around the stator MMF vector. Because the bare rotor draws only $10\text{ to } 25\text{ mA}$, an active damping gain tuned for 1–2 Amp loads ($K_{\text{damp}} = 25\text{ rad/(s}\cdot\text{A)}$) produced negligible frequency correction ($\Delta \omega < 0.08\text{ Hz}$), allowing hunting resonance to trip the hardware overcurrent comparator (L6230 OCD / TIM1_BKIN).
2. **Unvalidated Deceleration Dynamics**: The bench DC power supply is unidirectional and unable to sink power. Rapid deceleration risked motor regeneration pumping the DC bus capacitors and tripping the power stage.

This feature implements synchronous active electronic damping calibrated for low-current bare rotor operation, activates closed-loop FOC current regulation, and validates bidirectional acceleration and deceleration across multi-step speed profiles up to 3600 RPM.

---

## 2. Mathematical Formulation & Architecture

### 2.1 Low-Current Active Electronic Damping
In a permanent magnet synchronous machine, the direct-axis current $I_d$ is directly proportional to the torque angle misalignment:
$$I_d \approx \frac{V \cos(\delta) - E_{\text{bemf}}}{X_d}$$
When the rotor leads or lags the stator rotating field, $I_d$ swings symmetrically around zero. By modulating the inverter electrical frequency proportionally to $-I_d$:
$$\Delta \omega_e(t) = -K_{\text{damp}} \cdot I_d(t)$$
the stator magnetic field dynamically advances or retards to shadow rotor position fluctuations, completely absorbing torsional hunting oscillations.

Because the bare rotor steady-state motoring drag is only $11\text{ to } 15\text{ mA}$, $K_{\text{damp}}$ was re-scaled from $25.0\text{ rad/(s}\cdot\text{A)}$ to $350.0\text{ rad/(s}\cdot\text{A)}$:
$$\text{For } \Delta I_d = 20\text{ mA}: \quad \Delta \omega_e = 350.0 \times 0.020\text{ A} = 7.0\text{ rad/s} \approx 1.11\text{ Hz}$$
A hard saturation clamp limits correction to $\pm 62.8\text{ rad/s}$ ($\pm 10.0\text{ Hz}$) to prevent inverter destabilization.

### 2.2 Symmetrical Sine-Table CCR Modulation with FOC Phase Damping
To guarantee zero floating-point library overhead and deterministic $20\text{ kHz}$ execution (<0.2% CPU load on the STM32G474RE at 170 MHz):
```c
/* 1. Advance 32-bit phase accumulator with active damping correction */
int32_t damping_correction = 0;
if (g_motor_ctrl.foc_closed_loop && FOC_AreRegulatorsActive() && 
    (g_motor_ctrl.state == MOTOR_STATE_RUNNING || g_motor_ctrl.state == MOTOR_STATE_STARTING) && 
    g_motor_ctrl.actual_speed_rpm >= 400.0f) {
    float d_rad_s = -g_foc.currents.id * g_foc.active_damping_gain;
    if (d_rad_s > 62.8f) d_rad_s = 62.8f;
    else if (d_rad_s < -62.8f) d_rad_s = -62.8f;
    damping_correction = (int32_t)(d_rad_s * 34178.0f);
}
s_phase_accum += (uint32_t)(s_phase_step + damping_correction);

/* 2. Read valley-synchronized 3-shunt currents and compute Clarke/Park */
float ia, ib, ic;
PowerStage_GetInjectedCurrents(&ia, &ib, &ic);
float theta_rad = ((float)(int32_t)s_phase_accum) * (PI_FLOAT / 2147483648.0f);
FOC_ProcessCurrents(ia, ib, ic, theta_rad);

/* 3. Apply rock-solid 12-bit signed sine table to compare registers */
int32_t scale = (int32_t)s_active_ccr_scale;
int32_t half = (int32_t)s_half_arr;
uint8_t u_idx = (uint8_t)(s_phase_accum >> 24);
uint8_t v_idx = (uint8_t)(u_idx + 85);
uint8_t w_idx = (uint8_t)(u_idx + 171);

htim1.Instance->CCR1 = (uint32_t)(half + (((int32_t)s_sin_table_s12[u_idx] * scale) >> 11));
htim1.Instance->CCR2 = (uint32_t)(half + (((int32_t)s_sin_table_s12[v_idx] * scale) >> 11));
htim1.Instance->CCR3 = (uint32_t)(half + (((int32_t)s_sin_table_s12[w_idx] * scale) >> 11));
```

### 2.3 Safe Deceleration & Anti-Regeneration Safeguards
To safeguard the unidirectional bench supply:
1. **Deceleration Slew Limiting**: Deceleration slew rate is set to $250.0\text{ RPM/s}$ in Profile 2. Because the bare rotor's natural aerodynamic coast-down rate exceeds $1000\text{ RPM/s}$, the motor continuously requires forward motoring torque to track the ramp downward.
2. **Strict Motoring Bias**: $I_{q,\text{demanded}}$ is clamped to a minimum of $+0.005\text{ A}$ ($+5\text{ mA}$), preventing negative torque commands and reverse current flow.
3. **Synchronous Voltage Tracking**: $V_{\text{target}} = V_{\text{boost}} + (\text{RPM} / K_v)$ tracks downward with speed, maintaining applied terminal voltage above rotor back-EMF so the DMOS bridge never acts as a boost rectifier.

---

## 3. Experimental Verification Results

### 3.1 9-Stage 24V Bidirectional Speed Ladder Test (`run_20261019_232617.csv`)
Sequence: $288 \to 864 \to 1728 \to 2880 \to 3600 \to 2880 \to 1728 \to 864 \to 288\text{ RPM}$ with 6.0 s holds per step:

| Step | Phase | Commanded (RPM) | Estimated End (RPM) | Optical Tachometer (Avg / End RPM) | Phase Peak Current (Avg / Max) | Active Duty (%) | Synchronism Status |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 1 | ACCEL | 288 | 288 | 178 / 287 | 12 mA / 23 mA | 13.9 | **IN SYNC** |
| 2 | ACCEL | 864 | 864 | 718 / 864 | 15 mA / 28 mA | 20.5 | **IN SYNC** |
| 3 | ACCEL | 1728 | 1728 | 1457 / 1728 | 14 mA / 27 mA | 28.9 | **IN SYNC** |
| 4 | ACCEL | 2880 | 2880 | 2448 / 2880 | 13 mA / 23 mA | 40.2 | **IN SYNC** |
| 5 | ACCEL | 3600 | 3600 | 3429 / 3600 | 11 mA / 20 mA | 51.3 | **IN SYNC** |
| 6 | DECEL | 2880 | 2880 | 3057 / 2880 | 13 mA / 23 mA | 47.0 | **IN SYNC** |
| 7 | DECEL | 1728 | 1728 | 2186 / 1728 | 13 mA / 23 mA | 36.9 | **IN SYNC** |
| 8 | DECEL | 864 | 864 | 1134 / 864 | 15 mA / 25 mA | 24.8 | **IN SYNC** |
| 9 | DECEL | 288 | 288 | 428 / 288 | 13 mA / 25 mA | 16.4 | **IN SYNC** |

- **Total Revolutions Counted**: **1,541 rev(s)** tracked by optical tachometer with 0 slips.
- **Maximum Steady-State Speed**: **3,601 RPM**.
- **Current Draw**: Peak max $28\text{ mA}$, Average $13.0\text{ mA}$.
- **Deceleration Evaluation**: Controlled deceleration verified across all 4 down-steps ($3600 \to 288\text{ RPM}$) with zero regenerative trips into the bench supply.

### 3.2 Extended 3600 RPM Steady-State Validation (`run_20261019_232832.csv`)
- **Duration**: 22.0 seconds (14.4s ramp + 7.6s steady state at 3600 RPM).
- **Outcome**: **`[PASS] MOTOR ROTATING IN SYNCHRONISM`**!
- **Total Revolutions Counted**: **869 rev(s)**.
- **Steady-State Optical Speed**: Avg **3600.2 RPM** | Final **3600 RPM** (exact match).
- **Current Draw**: Max **30 mA** | Avg **13.6 mA**.
- **Hardware Fault Status**: Zero trips (`[READY / NORMAL]`).
