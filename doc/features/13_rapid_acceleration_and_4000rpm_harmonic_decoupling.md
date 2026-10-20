# Feature 13: Rapid Acceleration Evaluation, Dynamic Slew Configuration & 4000 RPM Carrier Harmonic Decoupling

## 1. Overview & Objectives

In accordance with project goals and user directives, this feature delivers:
1. **Rapid Acceleration Evaluation**: Tuning and validating higher acceleration slew rates ($250 \to 500 \to 1000\text{ RPM/s}$) tailored for the low-inertia bare rotor (`5010-360KV`, 7 pole pairs, no flywheel).
2. **Interactive Dynamic Slew Rate Control**: Live adjustment of acceleration and deceleration slew rates on the fly via UART console (`A<slew>\r`), live dashboard display on Row 22, and CLI integration (`--slew`).
3. **Root Cause Analysis & Elimination of the 3896.1 RPM Trip**: Identifying the exact $N = 44.0$ integer harmonic alias between the 20 kHz PWM carrier and the $454.55\text{ Hz}$ fundamental electrical frequency, and replacing raw $I_d$ damping with **DC-decoupled AC perturbation active damping**.
4. **Full 4000 RPM Bidirectional Deceleration Validation**: End-to-end multi-stage ladder testing up to 4000 RPM ($864 \to 1728 \to 2880 \to 4000 \to 2880 \to 1728 \to 864 \to 288\text{ RPM}$) with zero regenerative bus pumping, zero slips, and zero hardware overcurrent trips.

---

## 2. Technical Architecture & Analysis

### 2.1 Low-Inertia Acceleration Dynamics
The target motor is a bare outrunner rotor bell ($J \approx 3.0 \times 10^{-6}\text{ kg}\cdot\text{m}^2$) without a flywheel.
- At $80\text{ RPM/s}$ (original flywheel profile), accelerating from 0 to 4000 RPM required $50.0\text{ seconds}$.
- At $500\text{ RPM/s}$, 0 to 3600 RPM takes only $7.2\text{ seconds}$.
- At $1000\text{ RPM/s}$, 0 to 3600 RPM takes only $3.6\text{ seconds}$.

The inertial reaction torque required for $1000\text{ RPM/s}$ is:
$$\tau_{\text{accel}} = J \cdot \alpha = 3.0 \times 10^{-6}\text{ kg}\cdot\text{m}^2 \times \left(1000 \times \frac{2\pi}{60}\right) = 0.000314\text{ N}\cdot\text{m}$$
With motor torque constant $K_t \approx 0.0265\text{ N}\cdot\text{m/A}$, the additional inertial current draw is less than $12\text{ mA}$, well within the 1.2A continuous ceiling and 2.2A hardware trip threshold.

### 2.2 The 3896.1 RPM ($N = 44.0$) Carrier Harmonic Trip Mechanism
Prior tests at just under 4000 RPM experienced sporadic overcurrent trips latching the L6230 `DIAG` line (`TIM1_BKIN`).
Mathematical analysis revealed the exact frequency alignment at the trip point:
$$f_e = \frac{3896.1\text{ RPM} \times 7\text{ pole pairs}}{60} = 454.545\text{ Hz}$$
$$\text{Carrier Ratio } N = \frac{f_{\text{PWM}}}{f_e} = \frac{20,000\text{ Hz}}{454.545\text{ Hz}} = 44.0000$$

At an integer carrier ratio $N = 44$:
1. The 3-phase PWM sampling occurs at the exact same angular positions on every electrical fundamental cycle.
2. Because 44 is not divisible by 3, the three phases ($0^\circ, 119.53^\circ, 240.47^\circ$) experience a stationary sampling asymmetry.
3. In the synchronous $d-q$ frame, this asymmetry manifests as a **false DC $I_d$ offset**.
4. In the previous implementation, raw $I_d$ was multiplied by $K_{\text{damp}} = 350.0$, pushing the frequency correction $d\_\text{rad\_s}$ against its hard saturation clamp ($\pm 62.8\text{ rad/s}$).
5. With damping saturated by the false DC alias, dynamic headroom dropped to zero. Any physical perturbation caused the bare rotor to swing, triggering a massive reactive current surge ($> 2.0\text{ A}$) that tripped the L6230 internal high-side overcurrent detector ($I_{\text{SOVER}}$).

### 2.3 DC-Decoupled AC Perturbation Active Damping
To permanently eliminate DC harmonic alias saturation, a discrete high-pass filter was introduced to decouple steady-state/sampling DC from dynamic torsional hunting perturbations:
```c
/* DC Tracking Filter (cutoff ~6.4 Hz at 20 kHz): rejects stationary harmonic aliases & DC offset */
s_id_dc = (s_id_dc * 0.998f) + (g_foc.currents.id * 0.002f);
float id_ac = g_foc.currents.id - s_id_dc;

float d_rad_s = -id_ac * g_foc.active_damping_gain;
if (d_rad_s > 62.8f) d_rad_s = 62.8f;
else if (d_rad_s < -62.8f) d_rad_s = -62.8f;
damping_correction = (int32_t)(d_rad_s * 34178.0f);
```
- **At DC / Steady State**: $I_{d,\text{AC}} \to 0$, guaranteeing that $d\_\text{rad\_s} = 0$. The controller operates with 100% dynamic headroom available at all speeds.
- **At Mechanical Resonance (15–50 Hz)**: $I_{d,\text{AC}} \approx I_d$, applying full proportional restoring torque to quench hunting.

---

## 3. Implementation Details

### 3.1 Firmware Modifications
1. **`include/motor_control.h` & `src/motor_control.c`**:
   - Added `void Motor_SetSlewRates(float accel_slew_rpm_s, float decel_slew_rpm_s)`.
   - Expanded speed ceiling from 6000 RPM to 6500 RPM.
   - Integrated `s_id_dc` tracking filter in `Motor_PWM_UpdateISR`.
2. **`src/main.c`**:
   - Added interactive `A<slew>\r` parser to adjust acceleration and deceleration rates dynamically.
3. **`src/console.c`**:
   - Updated Row 22 to display active slew rate: `Commanded Torque Limit : 1.20 A | Slew: 500 RPM/s`.
   - Updated key legend: `[A<slew>] Slew`.
4. **`tools/run_motor_test.py`**:
   - Added `--slew` CLI parameter and automatic `a<slew>\r` configuration.

---

## 4. Hardware Verification & Test Results

### 4.1 Rapid Acceleration Verification (3600 RPM)
| Test Run | Slew Rate | Ramp Time (0-3600) | Optical Revs | Max Optical RPM | Steady RPM | Peak Phase Curr | Status |
|---|---|---|---|---|---|---|---|
| `run_20261019_233810` | $500\text{ RPM/s}$ | $7.2\text{ s}$ | $724\text{ rev}$ | $3601\text{ RPM}$ | $3600.2\text{ RPM}$ | $23\text{ mA}$ | **[PASS] IN SYNC** |
| `run_20261019_233836` | $1000\text{ RPM/s}$ | $3.6\text{ s}$ | $587\text{ rev}$ | $3601\text{ RPM}$ | $3600.0\text{ RPM}$ | $27\text{ mA}$ | **[PASS] IN SYNC** |

### 4.2 Carrier Harmonic Boundary Traversal (3750 – 3920 RPM)
| Test Run | Target RPM | Optical Revs | Max Optical RPM | Steady RPM | Peak Current | Status |
|---|---|---|---|---|---|---|
| `run_20261019_234939` | $3750\text{ RPM}$ | $616\text{ rev}$ | $3751\text{ RPM}$ | $3750.3\text{ RPM}$ | $27\text{ mA}$ | **[PASS] IN SYNC** |
| `run_20261019_235007` | $3800\text{ RPM}$ | $505\text{ rev}$ | $3801\text{ RPM}$ | $3799.8\text{ RPM}$ | $28\text{ mA}$ | **[PASS] IN SYNC** |
| `run_20261019_235030` | $3850\text{ RPM}$ | $630\text{ rev}$ | $3851\text{ RPM}$ | $3850.4\text{ RPM}$ | $25\text{ mA}$ | **[PASS] IN SYNC** |
| `run_20261019_235054` | $3880\text{ RPM}$ | $633\text{ rev}$ | $3881\text{ RPM}$ | $3880.3\text{ RPM}$ | $28\text{ mA}$ | **[PASS] IN SYNC** |
| `run_20261019_235120` | $3920\text{ RPM}$ | $634\text{ rev}$ | $3921\text{ RPM}$ | $3920.4\text{ RPM}$ | $28\text{ mA}$ | **[PASS] IN SYNC** |

### 4.3 Full 8-Stage 4000 RPM Bidirectional Deceleration Ladder (`run_20261019_235252`)
- **Total Revolutions Counted**: **1,277 revolutions**
- **Maximum Optical Speed**: **4000 RPM**
- **Peak Phase Current**: Max **28 mA**, Avg **13.7 mA**
- **Faults / Slips**: **0**

```
Step  | Phase   | Target  | Est End  | Real Optical (Avg/End) | Phase Peak (Avg/Max) | Duty %  | Status
---------------------------------------------------------------------------------------------------------
1     | ACCEL   | 864     | 864      | 312 / 851              | 13 mA / 23 mA        | 15.8    | IN SYNC
2     | ACCEL   | 1728    | 1728     | 1403 / 1728            | 13 mA / 23 mA        | 28.3    | IN SYNC
3     | ACCEL   | 2880    | 2880     | 2342 / 2879            | 12 mA / 28 mA        | 39.1    | IN SYNC
4     | ACCEL   | 4000    | 4000     | 3499 / 4000            | 14 mA / 23 mA        | 52.2    | IN SYNC
5     | DECEL   | 2880    | 2880     | 3379 / 2880            | 16 mA / 23 mA        | 50.5    | IN SYNC
6     | DECEL   | 1728    | 1728     | 2259 / 1728            | 15 mA / 28 mA        | 37.7    | IN SYNC
7     | DECEL   | 864     | 864      | 1172 / 864             | 14 mA / 22 mA        | 25.1    | IN SYNC
8     | DECEL   | 288     | 288      | 469 / 289              | 12 mA / 25 mA        | 16.9    | IN SYNC
```
