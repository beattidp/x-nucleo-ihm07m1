# Feature 00: Initial Implementation Plan: X-NUCLEO-IHM07M1 Motor Driver Evaluation on NUCLEO-G474RE

This document archives the initial baseline implementation plan for evaluating the **X-NUCLEO-IHM07M1** three-phase BLDC/PMSM motor driver expansion board (based on the ST **L6230** monolithic DMOS bridge) plugged into the **NUCLEO-G474RE** development board (STM32G474RE, ARM Cortex-M4F @ 170 MHz) using PlatformIO and STM32Cube HAL.

---

## 1. Comparative Evaluation: `X-CUBE-SPN7` vs. `X-CUBE-SPN8`

Both firmware packages in `tmp/` have been evaluated. Neither alone provides a turn-key solution for our target hardware, but they serve as complementary references:

| Feature / Metric | `X-CUBE-SPN7` (v1.1.0) | `X-CUBE-SPN8` (v1.0.2) | Project Integration Strategy |
| :--- | :--- | :--- | :--- |
| **Target Power Stage** | **X-NUCLEO-IHM07M1 (L6230)**<br>Single DMOS IC with 3 PWM inputs and 3 independent enable pins (`EN1..3`). | **X-NUCLEO-IHM08M1 (L6398)**<br>Discrete half-bridge drivers requiring 6 complementary PWM lines (`CCxE`/`CCxNE`). | **Adopt SPN7 exclusively** for hardware pin mapping, `EN1..3` GPIO sequencing, `CURRENT REF` biasing, and fault handling. |
| **Control Modes** | **Current Mode only**<br>Requires DAC/PWM current reference and hardware ETR current chopping (`PA12`). | **Voltage Mode & Current Mode**<br>Supports direct PWM duty cycle regulation via PI loop without ETR chopping. | **Adopt SPN8's Voltage Mode math**; voltage mode is far more reliable and easier to stabilize when motor parameters are unknown. |
| **Commutation Math** | 6-Step Library v1.0.0<br>Lookup-table demagnetization delay. | 6-Step Library v1.1.0<br>Dynamic auto-reload calculated demagnetization delay; cleaner PI structure. | **Adopt SPN8's algorithmic state machine** and timing calculations. |
| **MCU Support** | F030, F103, F302, F401 (Legacy). | F302, F401 (Legacy). | **Develop modern G474 BSP** natively in PlatformIO (170 MHz clock tree, dual 4 MSPS ADCs, CORDIC). |
| **Hall / FOC Support** | None (Sensorless 6-step only). | None (Sensorless 6-step only). | **Implement modular architecture** supporting Hall feedback and FOC. |

> [!TIP]
> **Synthesis**: We will extract the **L6230 hardware driver semantics from SPN7** and combine them with the **Voltage Mode commutation algorithms from SPN8**, ported cleanly to the **STM32G474RE**.

---

## 2. Flywheel Application Analysis: Balancing, Tuning, Spin-Up & Braking

The target motor will drive a **flywheel** undergoing dynamic speed changes during mechanical balancing, testing, and tuning. This imposes specific physics constraints that dictate our motor control strategy:

### 2.1 The Flywheel Challenges
1. **High Rotational Inertia ($J$)**:
   - Accelerating a high-inertia flywheel from a standstill requires high, controlled torque without stalling or losing synchronization.
   - Sensorless 6-step open-loop ramps frequently stall or draw excessive current under high-inertia loads because the rotor lags behind the electrical stepping angle.
2. **Torque Ripple & Unbalance Measurement Interference**:
   - 6-step trapezoidal control has an intrinsic **14% to 30% torque ripple** occurring 6 times per electrical revolution during phase commutations.
   - **Vibration Contamination**: In flywheel balancing, accelerometers and dynamic vibration sensors measure minuscule mass unbalance forces. Commutation torque ripple produces mechanical noise and acoustic harmonics that directly contaminate vibration sensor readings.
3. **Dynamic Speed Shifts & Rapid Settling**:
   - Balancing procedures require stepping rapidly between test speeds (e.g., 500 $\to$ 1500 $\to$ 3000 RPM) and holding speed tightly with zero hunting.
4. **Active 4-Quadrant Braking (Deceleration)**:
   - A spinning flywheel stores substantial kinetic energy ($E_k = \frac{1}{2} J \omega^2$).
   - Coasting down takes an unacceptable amount of time.
   - Passive shorting or plug braking causes violent current surges and mechanical shocks.
   - Regulated deceleration requires active, controlled negative torque.

### 2.2 Why Field Oriented Control (FOC) is Advantageous Here
- **Zero Commutation Torque Ripple**: Stator currents are purely sinusoidal and oriented orthogonally to rotor flux ($I_d = 0$, $I_q = \text{torque}$). Torque output is continuous and smooth, providing an ideal, vibration-free baseline for unbalance sensors.
- **Maximum Torque Per Ampere (MTPA) from 0 RPM**: With Hall sensors or an encoder, FOC guarantees non-stalling, smooth spin-up under heavy inertial loads.
- **Controlled 4-Quadrant Dynamic Braking**: Deceleration is achieved simply by demanding negative torque current ($I_q < 0$). The motor provides smooth, linearly controlled braking down to a dead stop.
- **High-Bandwidth Speed Regulation**: The inner torque/current loop runs at the PWM carrier frequency (20–30 kHz), allowing fast, critically damped speed transitions.
- **Hardware Match**: The STM32G474RE (hardware CORDIC accelerator computes $\sin/\cos$ in 4 clock cycles) and the X-NUCLEO-IHM07M1 (3 independent shunts with TSV994 op-amp) are fully equipped for 3-shunt FOC.

---

## 3. Phased Implementation Architecture

```text
+-------------------------------------------------------------------------------+
|                    Interactive Non-Scrolling ANSI Dashboard                   |
|                  (LPUART1 @ 115200 baud, PA2/PA3, VT100 UI)                   |
+---------------------------------------+---------------------------------------+
                                        | Telemetry & Commands
+---------------------------------------v---------------------------------------+
|                         Motor Control State Machine                           |
|       [IDLE] <--------> [STAGE 1: OPEN-LOOP / CHAR] <--------> [STAGE 2/3]    |
+---------------------------------------+---------------------------------------+
                                        | Modular Strategy
    +-----------------------------------+-----------------------------------+
    |                                   |                                   |
+---v---------------------------+ +-----v-----------------------+ +---------v-------------------+
| Stage 1: Bring-up & Char      | | Stage 2: Voltage 6-Step     | | Stage 3: 4-Quadrant FOC     |
| - Open-loop rotating vector   | | - Voltage mode (SPN8 logic) | | - Sinusoidal Park/Clarke    |
| - Safe motor spinning         | | - Hall sensored & BEMF      | | - CORDIC coprocessor math   |
| - Verify poles, Kv, direction | | - Direct duty-cycle control | | - Smooth braking & zero-rip |
+-------------------------------+ +-----------------------------+ +-----------------------------+
                                        |
+---------------------------------------v---------------------------------------+
|                        IHM07M1 / L6230 Hardware Layer                         |
|  - PWM: TIM1_CH1..3 (PA8, PA9, PA10) @ 20 kHz                                 |
|  - Enables: EN1..3 (PC10, PC11, PC12)                                         |
|  - Current Limit Reference: TIM16_CH1 (PB4)                                   |
|  - Emergency Trip / Fault: TIM1_BKIN (PA6)                                    |
|  - Telemetry ADC: VBUS (PA1), Temp (PC2), Pot (PB1), Shunts (PA0, PC1, PC0)   |
|  - Sensors: Hall / Encoder on TIM2 (PA15, PB3, PB10)                          |
+-------------------------------------------------------------------------------+
```

---

## 4. Hardware Pin Mapping & Conflict Avoidance

> [!WARNING]
> **Serial Console UART Selection**:
> - Reference project `../g474re-platformio` used `USART1` on `PA9`/`PA10`.
> - On X-NUCLEO-IHM07M1, `PA9` is **`IN2`** and `PA10` is **`IN3`** (PWM inputs).
> - **We use `LPUART1` on `PA2` (TX) and `PA3` (RX)**. This routes directly to the ST-LINK V3E Virtual COM Port by default (`SB17`/`SB23` ON) and eliminates any pin collision.

| Function | Pin | Nucleo Connector | STM32 Peripheral | Role in System |
| :--- | :--- | :--- | :--- | :--- |
| **PWM IN1 (U)** | `PA8` | CN10 pin 23 | `TIM1_CH1` | Phase 1 PWM drive |
| **PWM IN2 (V)** | `PA9` | CN10 pin 21 | `TIM1_CH2` | Phase 2 PWM drive |
| **PWM IN3 (W)** | `PA10` | CN10 pin 33 | `TIM1_CH3` | Phase 3 PWM drive |
| **Bridge EN1** | `PC10` | CN7 pin 1 | GPIO Output | Phase 1 Enable (Hi-Z control) |
| **Bridge EN2** | `PC11` | CN7 pin 2 | GPIO Output | Phase 2 Enable (Hi-Z control) |
| **Bridge EN3** | `PC12` | CN7 pin 3 | GPIO Output | Phase 3 Enable (Hi-Z control) |
| **DIAG / Fault** | `PA6` | CN10 pin 13 | `TIM1_BKIN` | L6230 overcurrent/thermal trip |
| **Current Ref** | `PB4` | CN10 pin 27 | `TIM16_CH1` | L6230 current limit reference |
| **Current Shunts** | `PA0`, `PC1`, `PC0` | CN7: 28, 36, 38 | `ADC1_IN1,7,6` | 3-phase current sensing ($0.33\,\Omega$) |
| **Bus Voltage** | `PA1` | CN7 pin 30 | `ADC1_IN2` | $V_{BUS}$ voltage divider ($\sim 19.15:1$) |
| **Temperature** | `PC2` | CN7 pin 35 | `ADC1_IN8` | Onboard NTC thermistor |
| **Potentiometer**| `PB1` | CN10 pin 24 | `ADC1_IN12` | 100 k$\Omega$ speed trimmer R42 |
| **Hall Sensors** | `PA15`, `PB3`, `PB10` | CN7:17, CN10:31,25 | `TIM2_CH1,2,3` | Hall sensors H1, H2, H3 (or Encoder) |
| **Red LED** | `PB2` | CN10 pin 22 | GPIO Output | IHM07M1 User/Fault LED D11 |
| **Green LED** | `PA5` | Onboard | GPIO Output | Nucleo LD2 heartbeat |
| **Blue Button** | `PC13` | Onboard | GPIO Input | Nucleo B1 user button |
| **Console UART** | `PA2` (TX), `PA3` (RX) | Onboard VCP | `LPUART1` | ST-LINK V3E Virtual COM Port |
