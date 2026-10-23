# Feature 19: Dual ADXL335 Accelerometer & Two-Plane Dynamic Flywheel Balancing Architecture

## 1. Executive Summary & Problem Formulation

Rotating rigid bodies such as high-inertia flywheels experience centrifugal forces resulting from mass asymmetries relative to the axis of rotation. Unbalance is generally composed of two distinct physical phenomena:
1. **Static Unbalance (Force Unbalance)**: The center of mass is displaced radially from the geometric axis of rotation, generating a rotating net radial force $F = m r \omega^2$ that acts uniformly along the axis.
2. **Couple Unbalance (Moment Unbalance)**: The principal axis of inertia is tilted relative to the geometric axis of rotation, creating an oscillating moment (rocking/tilting couple) that acts in opposite directions at opposite ends of the rotor.
3. **Dynamic Unbalance**: The general combination of static and couple unbalance, which cannot be measured or corrected in a single axial plane.

By deploying **two 3-axis ADXL335 analog accelerometers** positioned on **opposite diagonal corners of the frame** (e.g., top-front-left and lower-rear-right), both translational radial forces and rocking angular moments become fully observable. When paired with the 1-pulse-per-revolution (1 PPR) index signal from the **optical tachometer**, the system can extract synchronous 1X vibration vectors (amplitude and phase) and compute the exact correction masses and angular positions across two balancing planes using the **Influence Coefficient Method**.

---

## 2. ADXL335 Electrical Profile & Interfacing

### 2.1 Sensor Specifications (from `ADXL335BCPZ.pdf`)
- **Supply Voltage ($V_S$)**: $1.8\text{ V}$ to $3.6\text{ V}$ ($3.0\text{ V}$ nominal; operated at **$3.3\text{ V}$** from the Nucleo regulator).
- **Supply Current**: $350\,\mu\text{A}$ typical per sensor ($700\,\mu\text{A}$ total for both devices, negligible on 3V3 rail).
- **Measurement Range**: $\pm 3\text{ g}$ minimum, $\pm 3.6\text{ g}$ typical.
- **Sensitivity ($S$)**: Ratiometric to $V_S$. At $V_S = 3.3\text{ V}$, $S = 330\text{ mV/g}$ (approx. $33.6\text{ mV} / (\text{m/s}^2)$).
- **Zero-$g$ Bias ($V_{0g}$)**: Ratiometric to $V_S$. At $V_S = 3.3\text{ V}$, $V_{0g} = 1.65\text{ V}$ ($V_S / 2$).
- **Output Swing**: $0.1\text{ V}$ to $3.2\text{ V}$ under full load.
- **Output Impedance ($R_{FILT}$)**: $32\text{ k}\Omega \pm 15\%$ internal series resistor.

### 2.2 Bandwidth & Filter Capacitor Sizing
The user-selected filter capacitors ($C_X, C_Y, C_Z$) form a first-order low-pass filter with internal resistance $R_{FILT} = 32\text{ k}\Omega$:
$$f_{-3\text{dB}} = \frac{1}{2\pi \cdot 32\text{ k}\Omega \cdot C}$$

For flywheel testing up to 6000 RPM ($100\text{ Hz}$ rotational frequency):
- Standard breakout boards (e.g. GY-61) often carry $C = 0.1\,\mu\text{F}$, yielding $f_{-3\text{dB}} \approx 50\text{ Hz}$. At 3000 to 6000 RPM, this causes significant attenuation and phase delay.
- **Recommended Capacitor Value**: $C = 10\text{ nF}$ provides $f_{-3\text{dB}} \approx 500\text{ Hz}$, maintaining $< 0.15\text{ dB}$ attenuation and linear phase response for the 1X rotational fundamental up to $100\text{ Hz}$ (and harmonics up to 5X).
- **ADC Sample-and-Hold Interaction**: The external $10\text{ nF}$ filter capacitor acts as a low-impedance charge reservoir relative to the STM32G4's internal ADC sampling capacitor ($C_{ADC} \approx 5\text{ pF}$). The ratio $C_{ext} / C_{ADC} = 2000:1$ eliminates voltage sag during the ADC sampling phase.

---

## 3. Spatial Frame Kinematics & Coordinate Decomposition

Placing two triaxial sensors on diagonally opposite corners of the rectangular test frame provides complete 6-degree-of-freedom mechanical observability:
- **Sensor 1 (Top-Front-Left)**: Located at position $(+x_1, +y_1, +z_1)$ relative to rotor center of gravity.
- **Sensor 2 (Lower-Rear-Right)**: Located at position $(-x_2, -y_2, -z_2)$ relative to rotor center of gravity.

```
                  Top-Front-Left [Sensor 1]
                       +-------------------+
                      /|                  /|
                     / |                 / |
                    *--+----------------+  |
                    |  |   [Flywheel]   |  |
                    |  |       O        |  |
                    |  +----------------+--+
                    | /                 | /
                    |/                  |/
                    +-------------------+*
                                      Lower-Rear-Right [Sensor 2]
```

### Vibration Mode Separation
1. **Static / Pure Force Unbalance (Translational Mode)**:
   - Centrifugal force produces in-phase acceleration components at both ends:
     $$\vec{a}_{static} \approx \frac{\vec{a}_1 + \vec{a}_2}{2}$$
2. **Couple / Moment Unbalance (Rocking / Pitch-Yaw Mode)**:
   - Dynamic moment produces opposite-phase acceleration components:
     $$\vec{a}_{couple} \approx \frac{\vec{a}_1 - \vec{a}_2}{2}$$
3. **Axial Thrust / Swash**:
   - $Y$-axis signals measure axial wobble and bearing misalignment.

---

## 4. Hardware Pinout & Peripheral Assignment

To preserve the zero-jitter timing of the 20 kHz injected current loop on **`ADC1`**, the 6 accelerometer channels are assigned to **`ADC2`** and **`ADC4`**:

| Channel | Sensor Axis | MCU Pin | Physical Header Pin | Peripheral & Channel | Rationale |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **AIN 1** | **$X_1$ (Lateral Top)** | **`PA4`** | Arduino **`A2`** (Morpho `CN7:32`) | `ADC2_IN17` | Isolated; shield resistors $R_{75}, R_{76}$ unpopulated. |
| **AIN 2** | **$Y_1$ (Axial Top)** | **`PC4`** | Morpho **`CN10:34`** | `ADC2_IN5` | Isolated; directly adjacent to `CN10:32` (AGND). |
| **AIN 3** | **$Z_1$ (Vertical Top)** | **`PC5`** | Morpho **`CN10:6`** | `ADC2_IN11` | Isolated; adjacent to Morpho logic GND. |
| **AIN 4** | **$X_2$ (Lateral Bottom)**| **`PB11`** | Morpho **`CN10:18`** | `ADC2_IN14` | Isolated; adjacent to `CN10:20` (GND). |
| **AIN 5** | **$Y_2$ (Axial Bottom)** | **`PB12`** | Morpho **`CN10:16`** | `ADC4_IN3` (or `ADC1_IN11`)| Isolated; runs on independent ADC4 instance. |
| **AIN 6** | **$Z_2$ (Vertical Bottom)**| **`PC3`** *(mod)*<br>or **`PB1`** *(no-mod)*| Morpho **`CN7:37`**<br>or **`CN10:24`** | `ADC2_IN9`<br>or `ADC3_IN1` | **Option A**: Remove $R_{59}$ ($0\,\Omega$) to liberate $P_{C3}$.<br>**Option B**: Reallocate $P_{B1}$ in software. |
| **Power** | $V_S (+3.3\text{ V})$ | `3V3` | Morpho `CN7:16` / `CN6:4` | Power Rail | Filtered 3.3 V logic supply. |
| **Ground**| `GND / AGND` | `AGND` | Morpho `CN10:32` | Analog Ground | Clean ground return. |

---

## 5. Two-Plane Dynamic Balancing Mathematical Formulation

### 5.1 Synchronous 1X Harmonic Phasor Extraction
Rather than computing computationally expensive full-spectrum FFTs, the balancing engine uses **synchronous single-bin discrete Fourier transformation (synchronous quadrature demodulation)** referenced to the optical tachometer index pulse:

1. Let $T_0$ be the timestamp of the latest 1 PPR optical tachometer trigger, and $T_{rev}$ be the rotational period ($T_{rev} = 60 / \text{RPM}$).
2. For each sample $a_k = a(t_k)$ acquired at time $t_k$, the instantaneous rotor angle is:
   $$\theta_k = 2\pi \frac{(t_k - T_0) \pmod{T_{rev}}}{T_{rev}}$$
3. Over an integer number of revolutions $M$ ($N$ total samples):
   $$X_{real} = \frac{2}{N} \sum_{k=0}^{N-1} a_k \cos(\theta_k), \quad X_{imag} = \frac{2}{N} \sum_{k=0}^{N-1} a_k \sin(\theta_k)$$
4. The 1X synchronous vibration phasor is:
   $$\vec{V} = A_{1X} e^{j\phi_{1X}}, \quad A_{1X} = \sqrt{X_{real}^2 + X_{imag}^2}, \quad \phi_{1X} = \text{atan2}(X_{imag}, X_{real})$$

This technique achieves $> 60\text{ dB}$ rejection of motor switching noise, aerodynamic buffet, and structural harmonics outside the rotational fundamental frequency.

### 5.2 Two-Plane Influence Coefficient Method
Two correction planes (Plane 1 and Plane 2) are designated on the flywheel (e.g., upper circumferential ring and lower circumferential ring).

1. **Baseline Run**:
   Spin the rotor to balancing speed $\omega_0$. Measure baseline vibration phasors:
   $$\vec{V}_{10} \quad (\text{Sensor 1}), \quad \vec{V}_{20} \quad (\text{Sensor 2})$$
2. **Trial Run 1**:
   Attach a known trial weight $\vec{W}_{t1} = m_{t1} e^{j\theta_{t1}}$ on Plane 1. Spin up to $\omega_0$ and measure:
   $$\vec{V}_{11}, \quad \vec{V}_{21}$$
   Compute Plane 1 influence coefficients:
   $$\vec{\alpha}_{11} = \frac{\vec{V}_{11} - \vec{V}_{10}}{\vec{W}_{t1}}, \quad \vec{\alpha}_{21} = \frac{\vec{V}_{21} - \vec{V}_{20}}{\vec{W}_{t1}}$$
3. **Trial Run 2**:
   Remove $\vec{W}_{t1}$. Attach a known trial weight $\vec{W}_{t2} = m_{t2} e^{j\theta_{t2}}$ on Plane 2. Spin up to $\omega_0$ and measure:
   $$\vec{V}_{12}, \quad \vec{V}_{22}$$
   Compute Plane 2 influence coefficients:
   $$\vec{\alpha}_{12} = \frac{\vec{V}_{12} - \vec{V}_{10}}{\vec{W}_{t2}}, \quad \vec{\alpha}_{22} = \frac{\vec{V}_{22} - \vec{V}_{20}}{\vec{W}_{t2}}$$
4. **Correction Weight Solution**:
   To drive residual vibration to zero ($\vec{V}_1 = 0, \vec{V}_2 = 0$):
   $$\begin{bmatrix} \vec{\alpha}_{11} & \vec{\alpha}_{12} \\ \vec{\alpha}_{21} & \vec{\alpha}_{22} \end{bmatrix} \begin{bmatrix} \vec{W}_{c1} \\ \vec{W}_{c2} \end{bmatrix} = - \begin{bmatrix} \vec{V}_{10} \\ \vec{V}_{20} \end{bmatrix}$$
   The determinant is:
   $$\Delta = \vec{\alpha}_{11} \vec{\alpha}_{22} - \vec{\alpha}_{12} \vec{\alpha}_{21}$$
   The analytical solution for correction weights is:
   $$\vec{W}_{c1} = \frac{-\vec{V}_{10}\vec{\alpha}_{22} + \vec{V}_{20}\vec{\alpha}_{12}}{\Delta} = m_{c1} \angle \theta_{c1}$$
   $$\vec{W}_{c2} = \frac{-\vec{V}_{20}\vec{\alpha}_{11} + \vec{V}_{10}\vec{\alpha}_{21}}{\Delta} = m_{c2} \angle \theta_{c2}$$

### 5.3 Discrete Hole Weight Splitting
If the flywheel uses discrete tapped balancing holes (e.g., $N_h = 12$ holes spaced at $\Delta\theta = 30^\circ$):
- If the required angle $\theta_c$ falls between hole $k$ ($\theta_k$) and hole $k+1$ ($\theta_{k+1}$):
  $$m_k = m_c \frac{\sin(\theta_{k+1} - \theta_c)}{\sin(\theta_{k+1} - \theta_k)}, \quad m_{k+1} = m_c \frac{\sin(\theta_c - \theta_k)}{\sin(\theta_{k+1} - \theta_k)}$$
The firmware can automatically decompose the continuous correction phasor into discrete set-screw masses.

---

## 6. SCPI Protocol Extensions for Dynamic Balancing

The embedded SCPI parser will be extended with the `:BALance` and `:MEASure:VIBration` subsystems:

| Command | Type | Description |
| :--- | :--- | :--- |
| `:MEASure:VIBration:ALL?` | Query | Returns current 6-channel instantaneous acceleration `[X1,Y1,Z1,X2,Y2,Z2]` in $g$. |
| `:MEASure:VIBration:RMS?` | Query | Returns 6-channel RMS vibration amplitudes over the last 100 ms. |
| `:MEASure:VIBration:HARMonic? [1X\|2X]` | Query | Returns 1X fundamental amplitude ($g$) and phase (deg) for all axes. |
| `:BALance:STATe?` | Query | Returns balancing engine state (`IDLE`, `BASELINE`, `TRIAL1`, `TRIAL2`, `SOLVED`). |
| `:BALance:RESet` | Command | Resets balancing calibration memory and influence coefficients. |
| `:BALance:CALibrate:BASEline` | Command | Records current 1X vibration vectors as baseline ($\vec{V}_{10}, \vec{V}_{20}$). |
| `:BALance:CALibrate:TRIA1 <mass_g>, <angle_deg>` | Command | Ingests Trial 1 mass and records $\vec{V}_{11}, \vec{V}_{21}$. |
| `:BALance:CALibrate:TRIA2 <mass_g>, <angle_deg>` | Command | Ingests Trial 2 mass and records $\vec{V}_{12}, \vec{V}_{22}$. |
| `:BALance:SOLVe?` | Query | Computes and returns correction solution: `<M1_g>,<Ang1_deg>,<M2_g>,<Ang2_deg>`. |
| `:BALance:HOLEs:SPLIT? <holes>` | Query | Decomposes solution into discrete balance hole locations and screw weights. |
