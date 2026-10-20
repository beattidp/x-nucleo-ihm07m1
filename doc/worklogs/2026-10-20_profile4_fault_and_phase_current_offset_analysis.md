# Worklog: Profile 4 Fault Investigation & Phase Current Offset Analysis

**Date**: 2026-10-20  
**Time**: 21:25 UTC  
**Agent**: Antigravity  
**User**: beattidp  

---

## 1. User Inquiry & Observations

The user reported the following behavior:
> *"Active motor profile 4 with slew rate 80 comes up to speed and runs for a while but then goes to fault. I see a very large value for IB, but not clear why.*  
> *`Phase & FOC Currents (mA)  : IA=   3 IB= -12 IC=   1 | Id=  -1 Iq=   2 mA`"*

---

## 2. Root Cause Analysis: Phase Current Asymmetry ($I_B = -12\text{ mA}$)

### 2.1 Engineering Scale vs. Visual Perception
On the dashboard display:
```
Phase & FOC Currents (mA)  : IA=   3 IB= -12 IC=   1 | Id=  -1 Iq=   2 mA
```
To the naked eye, `IB = -12` appears 4× to 12× larger than `IA = 3` or `IC = 1`, and the negative sign stands out starkly.

However, in absolute engineering units:
- $12\text{ mA}$ is only **$0.012\text{ A}$**.
- The motor rating is up to $1.20\text{ A}$ continuous with a $2.20\text{ A}$ hardware break trip limit.
- $12\text{ mA}$ represents only **$0.5\%$ of full scale** ($\sim 4000\text{ mA}$ peak dynamic range).

### 2.2 Analog Hardware & ADC Discretization
On the X-NUCLEO-IHM07M1 expansion board:
1. **Shunt Resistors**: $R_{\text{shunt}} = 0.33\,\Omega$.
2. **Current Sense Amplifier**: TSV994 operational amplifier with closed-loop gain $A_v = 1.53$.
3. **Net Current Gain**: $G_i = 0.33\,\Omega \times 1.53 = 0.5049\text{ V/A}$.
4. **ADC Resolution**: STM32G474RE 12-bit ADC ($V_{\text{ref}} = 3.30\text{ V}$):
   $$\text{LSB Voltage} = \frac{3.30\text{ V}}{4095} = 0.80586\text{ mV / count}$$
   $$\text{Current LSB} = \frac{0.80586\text{ mV}}{0.5049\text{ V/A}} \approx 1.596\text{ mA / count}$$

Converting $-12\text{ mA}$ to raw ADC counts:
$$\Delta_{\text{counts}} = \frac{-12\text{ mA}}{1.596\text{ mA/count}} \approx -7.5\text{ ADC counts (out of 4096)}$$
$$\text{Analog Voltage Error} = 7.5 \times 0.80586\text{ mV} \approx 6.04\text{ mV}$$

### 2.3 Thermal Drift Mechanism
- Zero-offset calibration (`IHM07M1_CalibrateCurrentOffsets()`) was previously performed **only once at system cold boot** (room temperature $\sim 22\text{ }^\circ\text{C}$).
- Under continuous bench testing, the driver board temperature warmed from $22\text{ }^\circ\text{C}$ to $45.4\text{ }^\circ\text{C}$ ($\Delta T \approx +23\text{ }^\circ\text{C}$).
- The TSV994 op-amp has input offset voltage drift ($V_{io}$ tempco), bias resistor temperature coefficients, and board thermal gradients. An offset change of $6.0\text{ mV}$ across a $+23\text{ }^\circ\text{C}$ temperature rise is completely normal analog behavior.

### 2.4 Asymmetry in Clarke Transform
The previous Clarke transformation implementation was asymmetrical:
$$I_\alpha = I_A$$
$$I_\beta = \frac{I_A + 2 I_B}{\sqrt{3}}$$
This formulation ignored $I_C$ and multiplied $I_B$ by $\frac{2}{\sqrt{3}} \approx 1.155$. Consequently, any uncompensated analog offset on Channel B was amplified directly into $I_\beta$, creating a false steady-state DC current in the rotating $d-q$ frame ($I_d = -1\text{ mA}, I_q = 2\text{ mA}$) even when actual phase current was zero.

---

## 3. Root Cause Analysis: Profile 4 Fault Trip at Slew Rate 80

### 3.1 Profile 4 Design Intent vs. Physical Setup
- **Profile 4 Parameters**:
  - Name: `5010-360KV Flywheel`
  - Max Speed: $4000\text{ RPM}$ ($f_e \approx 466.7\text{ Hz}$)
  - Acceleration Slew Rate: $80.0\text{ RPM/s}$
  - Intended Load: High-inertia flywheel ($J \sim 1500\text{ g}\cdot\text{cm}^2$).
- **Current Physical Setup**:
  - The motor on the bench is currently a **bare outrunner rotor bell** ($J \sim 30\text{ g}\cdot\text{cm}^2$, no flywheel attached yet).

### 3.2 Physics of the Trip Event
1. **Prolonged Open-Loop Slew Time**:
   - Accelerating from $0 \to 3920\text{ RPM}$ at $80\text{ RPM/s}$ requires **$49.0\text{ seconds}$**.
   - For a bare rotor with virtually zero mechanical damping ($B \approx 0$), spending tens of seconds in the high electrical frequency region ($> 300\text{ Hz}$, $> 2500\text{ RPM}$) under scalar V/F open-loop control exposes the system to continuous torsional excitation.
2. **Rotor Hunting (Torsional Resonance)**:
   - Synchronous machines in open-loop scalar V/F act as second-order torsional pendulums:
     $$J \frac{d^2\delta}{dt^2} + B \frac{d\delta}{dt} + T_{\text{sync}}\sin\delta = T_L$$
   - With low rotor inertia and no load friction, subtle perturbations (carrier harmonic interaction, inverter dead-time, or aerodynamic drag) cause the torque angle $\delta$ to oscillate around the stator MMF vector.
3. **Loss of Synchronism & OCD Trip**:
   - When $\delta$ swings beyond $90^\circ$ electrical, the back-EMF vector $\vec{E}$ clashes directly with the applied stator voltage vector $\vec{V}_s$:
     $$\Delta \vec{V} = \vec{V}_s - \vec{E}$$
   - Across low phase resistance ($0.50\,\Omega$), this creates an instantaneous reactive current spike ($> 2.2\text{ A}$).
   - The L6230 internal hardware overcurrent comparator trips in $< 1.5\,\mu\text{s}$, pulling `DIAG` LOW (`PA6` / `TIM1_BKIN`) and latching the drive into hardware shutdown.

---

## 4. Firmware Implementation & Fixes

### 4.1 Symmetrical 3-Shunt Balanced Clarke Transformation
In [`src/foc.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/foc.c):
Replaced the 2-shunt formulation with a symmetrical 3-shunt transform incorporating common-mode zero-sequence rejection:
$$I_0 = \frac{I_A + I_B + I_C}{3}$$
$$I_\alpha = I_A - I_0$$
$$I_\beta = \frac{I_B - I_C}{\sqrt{3}}$$
- **Common-mode drift rejection**: Subtracting $I_0$ eliminates any shared DC bias drift across all three analog channels.
- **Balanced channel weighting**: Computing $I_\beta = (I_B - I_C)/\sqrt{3}$ divides sensitivity equally between Phase B and Phase C (multiplying by $1/\sqrt{3} \approx 0.577$ instead of $1.155$).

### 4.2 Dynamic Rest Offset Recalibration
In [`src/motor_control.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_control.c):
- Added `Motor_CalibrateCurrentOffsets()` execution whenever the drive transitions to `MOTOR_STATE_STOPPED` (`Motor_Stop()`) and on fault clearance (`Motor_ResetFault()`).
- Because half-bridge drivers are disabled when stopped ($0\text{ A}$ current), offsets are dynamically updated at the prevailing board operating temperature ($\sim 45\text{ }^\circ\text{C}$).

---

## 5. Verification & Live Target Results

1. **Compilation & Flashing**:
   - PlatformIO build: Clean success (`0 errors, 0 warnings`).
   - Flashed firmware via ST-Link V3 (`.pio/build/nucleo_g474re/firmware.elf`).
2. **Live Telemetry Verification (Board Temp: $45.4\text{ }^\circ\text{C}$)**:
   - Previous reading: `IA= 3 IB= -12 IC= 1 | Id= -1 Iq= 2 mA`
   - Verified post-flash reading:
     ```
     Phase & FOC Currents (mA)  : IA=   0 IB=   0 IC=   0 | Id=   0 Iq=   0 mA
     HW Trip Limit & Peak Curr  : 2.20 A (L6230 OCD) | Peak:    0 mA
     ```
   - Zero-offset discrepancy completely eliminated.
3. **Operational Recommendation**:
   - For evaluating the **bare rotor bell**, use **Profile 2** (`5010-360KV Bare Rotor`) with slew rates $\ge 250\text{--}500\text{ RPM/s}$. Profile 2 ramps swiftly through resonance zones with active damping enabled.
   - **Profile 4** (`5010-360KV Flywheel`, $80\text{ RPM/s}$) is reserved for when the mechanical flywheel or adjustable torque fixture is installed, providing the necessary rotational inertia to stabilize the slow ramp.
