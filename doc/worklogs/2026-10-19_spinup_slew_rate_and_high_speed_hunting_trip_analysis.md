# Worklog: Spin-Up Slew Rate Analysis & 4000 RPM High-Speed Hunting Trip Investigation

**Date**: 2026-10-19  
**Time**: 02:45 UTC  
**Agent**: Antigravity  
**User**: beattidp  

---

## 1. Context & User Inquiries
1. **Slow Spin-Up Time**: *"Spin-up time is very slow -- is this deliberate?"*
2. **High-Speed Overcurrent Trip**: *"Potentiometer was adjusted for just under 4000 rpm, disabled potentiometer mode, and started run. At just under 4000 rpm ran fine for some time, then suddenly fault/overcurrent: ... Total Revolutions Counted: 25556 revs, Driver Status: [FAULT / OVERCURRENT TRIPPED]"*

---

## 2. Part 1: Spin-Up Acceleration Rate Analysis

### Root Cause
- In `src/motor_profiles.c`, Profile 2 (`Flywheel 5010-360KV`) specifies:
  ```c
  .accel_slew_rpm_s = 80.0f  /* 80 RPM per second */
  ```
- Ramping from 0 to 4000 RPM at 80 RPM/s takes:
  $$\Delta t = \frac{4000\text{ RPM}}{80\text{ RPM/s}} = 50.0\text{ seconds}$$
  plus 1.0 second DC rotor alignment (`startup_timer_ms < 1000`) and S-curve deceleration tapering within 40 RPM of the target (where the effective slew rate drops to 15%, or 12 RPM/s).

### Design Intent vs. Current Test Setup
- **Original Intent**: Profile 2 was engineered for a heavy brass inertia flywheel ($J \sim 1500\text{ g}\cdot\text{cm}^2$). In open-loop scalar V/F control without rotor angle or current feedback, high acceleration ($J \cdot \alpha$) creates a large inertial reaction torque. If accelerated faster than 80 RPM/s, the motor's synchronous pull-out torque is exceeded, causing loss of synchronism, stall, and immediate overcurrent tripping.
- **Current Setup**: The user noted that the motor is currently running as a **bare rotor without a flywheel**. The bare outrunner rotor bell has an inertia of only $J \sim 25\text{--}35\text{ g}\cdot\text{cm}^2$ ($\sim 50\times$ lower).
- **Conclusion**: The slow spin-up is completely deliberate in software for the heavy flywheel, but unnecessarily sluggish for a bare rotor. A bare rotor can comfortably accelerate at **400 to 800 RPM/s** (reaching 4000 RPM in 5 to 10 seconds).

---

## 3. Part 2: High-Speed Overcurrent Trip at 3974 RPM (25,556 Revolutions)

### Telemetry Breakdown
- **Runtime at Target**: 25,556 mechanical revolutions at $\approx 3974\text{ RPM}$ corresponds to:
  $$\text{Duration} = \frac{25556\text{ revs}}{3974\text{ rev/min}} = 6.43\text{ minutes} = 385.8\text{ seconds}$$
- **Electrical Frequency**: $f_e = \frac{3974 \times 7}{60} = 463.6\text{ Hz}$ ($27,818\text{ electrical RPM}$).
- **Bus Voltage ($V_{\text{bus}}$)**: $24.22\text{ V}$ (healthy and stable).
- **Driver Board Temperature**: $38.5^\circ\text{C}$ (far below L6230 $165^\circ\text{C}$ thermal shutdown; TSD is definitively ruled out).
- **Applied Voltage & Duty**: At 57.6% duty, applied stator voltage is $V_s \approx 13.95\text{ V}$. Motor back-EMF is $V_{\text{BEMF}} = \frac{3974}{360} = 11.04\text{ V}$. Headroom is $> 10\text{ V}$.
- **Fault Indicator**: L6230 `DIAG` asserted LOW, latching the STM32 `TIM1_BKIN` hardware break input and terminating PWM in $< 1.5\,\mu\text{s}$.

### Physical Mechanisms of the Trip
1. **Open-Loop Rotor Hunting (Mid/High-Frequency Torsional Resonance)**:
   - A permanent magnet synchronous motor in open-loop scalar V/F behaves as a second-order torsional pendulum:
     $$J \frac{d^2\delta}{dt^2} + B \frac{d\delta}{dt} + T_{\text{sync}}\sin\delta = T_L$$
   - A bare rotor has almost zero mechanical damping ($B \approx 0$). Unlike induction motors, PMSMs lack amortisseur (damper) cage windings.
   - Without closed-loop current regulation to supply active damping, scalar V/F PMSM drives have a known underdamped torsional resonance mode. Over continuous high-speed operation (at $463.6\text{ Hz}$ electrical), subtle perturbations (inverter dead-time harmonics, PWM switching ripple, power supply ripple, or aerodynamic turbulence) excite and grow this torsional oscillation.
2. **Thermal Drift of Stator Resistance and Rotor Flux**:
   - Running at ~4000 RPM for 6.4 minutes creates continuous stator $I^2 R$ losses and iron core eddy-current losses ($P_{\text{core}} \propto f_e^2$).
   - Copper winding temperature rises ($R_s$ increases by $+0.393\%/^\circ\text{C}$), while rotor magnet temperature rises ($B_r$ decreases by $-0.11\%/^\circ\text{C}$).
   - This slightly reduces the magnetic restoring spring stiffness ($K_s = \frac{p \psi_m V_s}{L_s}\cos\delta$), progressively shrinking the stable basin of attraction in open-loop V/F.
3. **The Trip Event (Instantaneous Reactive Cross-Current Surge)**:
   - When the rotor angle $\delta$ momentarily swung past its stability boundary, the vector difference between stator voltage $\vec{V}_s$ and internal back-EMF $\vec{E}$ diverged sharply:
     $$\Delta \vec{V} = \vec{V}_s - \vec{E}$$
   - With low phase resistance ($0.50\,\Omega$) and low inductance ($\sim 150\,\mu\text{H}$), an angular excursion of even $15^\circ - 20^\circ$ causes an instantaneous cross-current surge:
     $$\Delta I = \frac{|\vec{V}_s - \vec{E}|}{|Z_s|} > 3\text{--}5\text{ A}$$
   - The L6230 internal hardware comparator (measuring shunt resistors against the 2.2A $V_{\text{REF}}$ threshold) immediately detected this surge, pulling `DIAG` LOW in $< 1.5\,\mu\text{s}$ and latching the drive into hardware shutdown.

---

## 4. Architectural Resolution: Field-Oriented Control (FOC)
- This failure demonstrates the fundamental limitation of open-loop scalar V/F control: lack of active current regulation and angle damping.
- In **Field-Oriented Control (FOC)**:
  - Currents are transformed to $d\text{--}q$ rotating coordinates using the on-chip CORDIC hardware accelerator.
  - Dual PI current loops run synchronously at 20 kHz ($50\,\mu\text{s}$ response time):
    * The $d$-axis regulator actively enforces $I_d = 0\text{ A}$, eliminating demagnetizing reactive current surges.
    * The $q$-axis regulator controls torque and provides strong electronic damping against rotor oscillations.
  - Rotor hunting is completely suppressed, preventing overcurrent trips even during load or speed transients.

---

## 5. Next Steps
1. Update Profile 2 (or add a dedicated "Bare Rotor 5010-360KV" profile) with `accel_slew_rpm_s = 500.0f` so spin-up takes 8 seconds instead of 50 seconds.
2. Implement dual PI current regulators on the existing CORDIC FOC core to eliminate open-loop hunting and overcurrent trips.
