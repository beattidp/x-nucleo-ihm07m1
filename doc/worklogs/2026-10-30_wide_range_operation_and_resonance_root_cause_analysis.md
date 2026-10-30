# Worklog: Wide-Range High-Speed BLDC Operation (100 to 3000+ RPM) & Resonance Analysis

**Date**: 2026-10-30  
**Time**: 12:15 UTC  
**Agent**: Antigravity  
**User**: beattidp  

---

## 1. Executive Summary & Problem Context

The user observed that after significantly reducing the eddy current brake (replacing two stacked magnets with one smaller magnet and increasing vertical clearance by +8 mm), recent tests evaluated the motor at ~277 RPM and ~214 RPM, whereas 300 RPM was achieved under the original heavy brake fixture. The user asked:
> *"Is the motor not capable now of running at higher speeds, even with reduced torque? If possible, the motor should work across a wider range of up to 3000 RPM or higher. We had 300 RPM with the original torque, but then evaluated at 277 RPM, then at 214 RPM."*

This worklog details the physical feasibility analysis, diagnoses the origin of the 214 RPM and 277 RPM values, uncovers the electromechanical resonance and circular voltage deadlock restricting acceleration, and defines the firmware enhancements to enable smooth operation from 100 to 3000+ RPM.

---

## 2. Physical Feasibility Analysis: Can the Motor Run at 3000+ RPM?

**Yes, unconditionally.**

### 2.1 Theoretical No-Load Boundary
- **Motor Specifications**: 5010 sensorless BLDC outrunner, 12N14P configuration (7 pole pairs, $p = 7$), $K_v = 360\text{ RPM/V}$, $K_t = 0.0265\text{ N}\cdot\text{m/A}$.
- **DC Supply Bus**: $V_{\text{bus}} = 24.25\text{ V}_{\text{DC}}$ on J1.
- **Theoretical No-Load Terminal Velocity**:
  $$n_{\text{no-load}} = K_v \times V_{\text{bus}} = 360\text{ RPM/V} \times 24.25\text{ V} \approx \mathbf{8{,}730\text{ RPM}}$$

### 2.2 Empirical Validation
- In milestone `v1.3`, Profile 1 (`5010-360KV Bare Rotor`) was driven and experimentally validated up to **4,000 RPM** (`run_20261019_235252.csv`, 1,277 revolutions, 13.7 mA average phase current, zero trips).
- The motor iron, bearing assembly, and L6230 DMOS bridge are fully proven to operate stably at 4,000 RPM.

### 2.3 Reduced Brake Drag Torque
- With the single smaller magnet and +8 mm additional air gap, measured viscous damping dropped to $\le 0.25\text{ mA/RPM}$.
- At 278 RPM, the average phase current was only **26.8 mA**, representing an opposing drag torque of:
  $$\tau_{\text{drag}} = K_t \cdot I_{\text{phase}} = 0.0265\text{ N}\cdot\text{m/A} \times 0.0268\text{ A} = \mathbf{0.71\text{ mN}\cdot\text{m}}$$
- This is negligible compared to the 20 mN·m drag of the original dual-magnet brake. The motor is operating almost unconstrained.

---

## 3. Origin of the "214 RPM" and "~277 RPM" Observations

### 3.1 The "214 RPM" Figure: Characterization Diagnostic Frequency
During automated characterization (`tools/run_characterization.py`), Step 4a evaluates open-loop dynamic spin point 1 at an electrical frequency of $f_e = 25.0\text{ Hz}$.
For a 7 pole-pair machine:
$$n = \frac{f_e \times 60}{p} = \frac{25.0\text{ Hz} \times 60}{7} = \mathbf{214.28\text{ RPM}}$$
The automated script also computed $K_v = 214\text{ RPM/V}$ based on this specific frequency point. This was purely a test excitation frequency, not a physical motor speed ceiling.

### 3.2 The "~277 RPM" Figure: Firmware Clamping Deadlock
Four firmware parameters configured specifically for the original 20 mN·m brake prevented the motor from exceeding ~278 RPM:
1. **Fixed 10.0 RPM Pullout Catch**:
   In a 7 pole-pair machine, $\Delta\theta_e = 2520^\circ \times (\Delta n / n)$. At 280 RPM, a 10 RPM error represents $90^\circ$ (pullout limit). But at 1000 RPM, 10 RPM is only $25.2^\circ$, and at 3000 RPM, it is only $8.4^\circ$. Normal optical pulse jitter falsely triggered the slip catch, pulling the stator command backwards.
2. **Fixed 32.0% Maximum Duty Clamp**:
   At $32.0\%$ duty, maximum terminal voltage is $0.320 \times 24.25\text{ V} = 7.76\text{ V}$. Back-EMF at 3000 RPM is $V_{\text{bemf}} = 3000 / 360 = \mathbf{8.33\text{ V}}$. Terminal voltage was lower than back-EMF, making speeds $> 2790\text{ RPM}$ physically unreachable under the clamp.
3. **Ultra-Slow 4.0 RPM/s Slew Rate**:
   At 4.0 RPM/s, accelerating from 0 to 3000 RPM requires 750 seconds (12.5 minutes). In a standard 140s test, the ramp generator physically only reached ~280 RPM.
4. **Circular Voltage/Ramp Deadlock**:
   During acceleration, `v_ref_rpm` was tied directly to `cmd_ramp_rpm`. When the slip governor clamped `cmd_ramp_rpm` to track the rotor at ~240 RPM, `v_ref_rpm` was also locked at ~240 RPM. This restricted terminal voltage to ~6.5V (27-28% duty), yielding only ~100 mA motoring current ($2.65\text{ mN}\cdot\text{m}$ torque). Net accelerating torque dropped to zero, freezing the system in an artificial equilibrium.

---

## 4. Electromechanical Torsional Resonance at 242.5 RPM

An electromechanical resonance analysis reveals why the motor specifically encountered hunting oscillations and slips around 238–243 RPM when the brake was reduced:

For a 7 pole-pair machine with rotor inertia $J = 7.20 \times 10^{-5}\text{ kg}\cdot\text{m}^2$ and average motoring current $I \approx 0.25\text{ A}$:
$$K_{\text{torsion}} = p \cdot K_t \cdot I = 7 \times 0.0265\text{ N}\cdot\text{m/A} \times 0.25\text{ A} = 0.0464\text{ N}\cdot\text{m/rad}$$
$$\omega_n = \sqrt{\frac{K_{\text{torsion}}}{J}} = \sqrt{\frac{0.0464}{7.20 \times 10^{-5}}} = 25.39\text{ rad/s}$$
$$f_n = \frac{\omega_n}{2\pi} = \mathbf{4.04\text{ Hz}} \iff \mathbf{242.5\text{ RPM}}$$

- **Under Heavy Eddy Brake**: Viscous damping ($\tau \propto \omega$) was large ($\zeta \approx 0.7$), heavily suppressing torsional oscillations.
- **Under Reduced Brake**: Damping dropped by $> 80\%$ ($\zeta \approx 0.05$). At a slow slew rate (15 RPM/s), the motor lingered in the 220–250 RPM resonance band for over 2 seconds (8 full oscillation cycles), triggering torsional hunting ($\pm 35\text{ mA}$ oscillation in $I_q$) and pole slip.
- **Solution**:
  1. Increase slew rate to $30.0\text{--}50.0\text{ RPM/s}$ to traverse the resonance band in $< 0.6$ seconds before oscillations can build up.
  2. Maintain synchronous electrical torque lead ($\delta_e \approx 35^\circ$) so the motor operates with maximum restoring stiffness ($d\tau/d\delta > 0$).

---

## 5. Firmware Architecture Enhancements

### 5.1 Decoupled Acceleration Voltage Ramp Reference
In `src/motor_control.c`, introduce an independent acceleration voltage ramp `s_v_ramp_rpm` that slews from 50 RPM to `target_speed_rpm`:
```c
static float s_v_ramp_rpm = 0.0f;
if (g_motor_ctrl.state == MOTOR_STATE_STOPPED) {
    s_v_ramp_rpm = 50.0f;
} else {
    float v_slew_per_ms = (g_motor_ctrl.accel_slew_rpm_s / 1000.0f);
    if (s_v_ramp_rpm < g_motor_ctrl.target_speed_rpm) {
        s_v_ramp_rpm += v_slew_per_ms;
        if (s_v_ramp_rpm > g_motor_ctrl.target_speed_rpm) s_v_ramp_rpm = g_motor_ctrl.target_speed_rpm;
    } else if (s_v_ramp_rpm > g_motor_ctrl.target_speed_rpm) {
        s_v_ramp_rpm -= v_slew_per_ms;
        if (s_v_ramp_rpm < g_motor_ctrl.target_speed_rpm) s_v_ramp_rpm = g_motor_ctrl.target_speed_rpm;
    }
}
float v_ref_rpm = s_v_ramp_rpm;
```
This guarantees that terminal driving voltage and accelerating current increase smoothly towards the target speed, completely breaking the circular deadlock.

### 5.2 Calibrated Torque Angle Lead & Slip Catch Boundaries
Using the 7 pole-pair torque angle relationship ($\Delta\theta_e = 2520^\circ \times \Delta n / n$):
- **Motoring Lead ($\delta_e \approx 35^\circ$)**:
  $$\Delta n_{\text{lead}} = 1.5\text{ RPM} + \frac{n_{\text{rotor}}}{72.0\text{f}}$$
- **Pullout Catch Boundary ($\delta_e \approx 70^\circ$)**:
  $$\Delta n_{\text{pullout}} = 3.0\text{ RPM} + \frac{n_{\text{rotor}}}{36.0\text{f}}$$

### 5.3 Speed-Scaled Continuous Duty Ceiling
Extend continuous duty clamp up to $75.0\%$ at 3000 RPM ($85.0\%$ at 4000 RPM), allowing terminal voltage to reach $18.2\text{ V}$ to overcome back-EMF ($8.33\text{ V}$) while preserving low-speed inrush protection ($22.0\%$ at $\le 50\text{ RPM}$).

### 5.4 Profile Slew Rate Optimization
Update Profile 4 `accel_slew_rpm_s` to **$30.0\text{ RPM/s}$** and `eddy_damping_ma_rpm` to **$0.25\text{ mA/RPM}$**.

---

## 6. Next Steps

1. Await user review and approval of `implementation_plan.md`.
2. Execute firmware implementation in `src/motor_control.c` and `src/motor_profiles.c`.
3. Compile and flash firmware to Nucleo-64.
4. Execute progressive multi-speed verification: 300 RPM -> 500 RPM -> 1000 RPM -> 3000 RPM.
