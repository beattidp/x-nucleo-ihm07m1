# Walkthrough: X-NUCLEO-IHM07M1 Motor Driver Evaluation on NUCLEO-G474RE

We have prepared and built the development project for evaluating the **X-NUCLEO-IHM07M1** three-phase BLDC motor driver expansion board (L6230 monolithic DMOS bridge) paired with the **NUCLEO-G474RE** development board (STM32G474RE @ 170 MHz) using PlatformIO and STM32Cube HAL.

---

## 1. Key Engineering Findings & Architectural Decisions

### 1.1 Resolution of Critical Serial Console Pin Collision
* In the reference project (`../g474re-platformio`), the serial console used `USART1` on `PA9` (TX) and `PA10` (RX).
* On the **X-NUCLEO-IHM07M1**, `PA9` is wired to **`IN2`** and `PA10` is wired to **`IN3`** (PWM inputs for Phase 2 and Phase 3 of the L6230 driver). Using `PA9`/`PA10` would transmit UART pulses directly into the motor power stage.
* **Resolution**: The serial console is configured on **`LPUART1` (`PA2` TX, `PA3` RX) @ 115200 baud**. On the NUCLEO-G474RE board (MB1367), `PA2`/`PA3` connects directly to the ST-LINK V3E Virtual COM Port by default via solder bridges `SB17` and `SB23`, completely eliminating pin collisions and operating over the single ST-Link USB cable.

### 1.2 Evaluation of `X-CUBE-SPN7.zip` vs. `x-cube-spn8.zip`
* **`X-CUBE-SPN7` (v1.1.0)** is the genuine ST package for the **X-NUCLEO-IHM07M1 (L6230)**. It provided the exact hardware abstraction semantics:
  * PWM on `TIM1`: `IN1` (`PA8`), `IN2` (`PA9`), `IN3` (`PA10`).
  * Phase enable GPIOs: `EN1` (`PC10`), `EN2` (`PC11`), `EN3` (`PC12`).
  * Current reference threshold: `CURRENT REF` on `PB4` (`CN10 pin 27`).
  * Hardware fault input: `DIAG` on `PA6` (`TIM1_BKIN`).
* **`X-CUBE-SPN8` (v1.0.2)** targets the discrete L6398 gate driver (IHM08M1), but provided superior, modernized **Voltage Mode** 6-step commutation algorithms. In contrast, SPN7 was locked strictly to peak current chopping with external ETR comparator triggering.
* **Architecture Implemented**: We combined the **L6230 hardware driver from SPN7** with the **Voltage Mode commutation logic from SPN8**, ported natively to the STM32G474RE.

### 1.3 Flywheel Application Strategy (Balancing, Testing, and FOC)
For a motor driving a flywheel undergoing dynamic speed changes and unbalance measurements:
* **Torque Ripple Elimination**: 6-step trapezoidal drive has an inherent 14%–30% commutation torque ripple that excites mechanical resonances and pollutes unbalance vibration sensor data. Field Oriented Control (FOC) uses sinusoidal excitation to eliminate torque ripple.
* **High-Inertia Spin-Up**: FOC provides Maximum Torque Per Ampere (MTPA) starting smoothly from 0 RPM without the blind stalling typical of sensorless 6-step startup ramps.
* **Active 4-Quadrant Dynamic Braking**: Kinetic energy stored in the flywheel can be decelerated smoothly and linearly by commanding negative torque current ($I_q < 0$) without harsh plug-braking shocks.

---

## 2. Hardware Pinout & Peripheral Assignment

| Signal / Function | MCU Pin | Morpho Connector | Peripheral / Mode | Hardware Description |
| :--- | :--- | :--- | :--- | :--- |
| **PWM Phase U (IN1)** | `PA8` | CN10 pin 23 | `TIM1_CH1` (AF6) | High-side PWM input Phase 1 (20 kHz center-aligned) |
| **PWM Phase V (IN2)** | `PA9` | CN10 pin 21 | `TIM1_CH2` (AF6) | High-side PWM input Phase 2 (20 kHz center-aligned) |
| **PWM Phase W (IN3)** | `PA10` | CN10 pin 33 | `TIM1_CH3` (AF6) | High-side PWM input Phase 3 (20 kHz center-aligned) |
| **Bridge Enable EN1** | `PC10` | CN7 pin 1 | GPIO Output PP | Active-HIGH enable for Phase 1 half-bridge |
| **Bridge Enable EN2** | `PC11` | CN7 pin 2 | GPIO Output PP | Active-HIGH enable for Phase 2 half-bridge |
| **Bridge Enable EN3** | `PC12` | CN7 pin 3 | GPIO Output PP | Active-HIGH enable for Phase 3 half-bridge |
| **DIAG / Fault** | `PA6` | CN10 pin 13 | `TIM1_BKIN` (AF6) | Hardware break input from L6230 open-drain fault line |
| **Current Reference** | `PB4` | CN10 pin 27 | `TIM16_CH1` (AF1) | PWM reference filtered through R21/C16 to set trip limit |
| **Hall Sensor H1** | `PA15` | CN7 pin 17 | GPIO Input (Pull-up) | Hall sensor Phase A (or Encoder A) |
| **Hall Sensor H2** | `PB3` | CN10 pin 31 | GPIO Input (Pull-up) | Hall sensor Phase B (or Encoder B) |
| **Hall Sensor H3** | `PB10` | CN10 pin 25 | GPIO Input (Pull-up) | Hall sensor Phase C (or Encoder Z) |
| **Bus Voltage Sensing** | `PA1` | CN7 pin 30 | `ADC1_IN2` | Resistor divider ($19.15:1$ ratio, 8 V to 48 V nominal) |
| **Driver Temperature** | `PC2` | CN7 pin 35 | `ADC1_IN8` | NTC 10 k$\Omega$ sensor placed near the L6230 |
| **Speed Potentiometer**| `PB1` | CN10 pin 24 | `ADC1_IN12` | 100 k$\Omega$ trimmer resistor R42 on expansion board |
| **Phase A Shunt** | `PA0` | CN7 pin 28 | `ADC1_IN1` | Phase A current ($0.33\,\Omega$ shunt via TSV994 op-amp) |
| **Phase B Shunt** | `PC1` | CN7 pin 36 | `ADC1_IN7` | Phase B current ($0.33\,\Omega$ shunt via TSV994 op-amp) |
| **Phase C Shunt** | `PC0` | CN7 pin 38 | `ADC1_IN6` | Phase C current ($0.33\,\Omega$ shunt via TSV994 op-amp) |
| **Shield Red LED** | `PB2` | CN10 pin 22 | GPIO Output PP | Fault / Indicator LED D11 on IHM07M1 |
| **Nucleo Green LED** | `PA5` | Onboard | GPIO Output PP | Heartbeat indicator LED LD2 |
| **Nucleo Blue Button** | `PC13` | Onboard | GPIO Input (Pull-up) | B1 User push button (Run / Stop toggle) |
| **Console UART TX** | `PA2` | Onboard VCP | `LPUART1_TX` (AF12)| ST-LINK V3E Virtual COM Port TX @ 115200 8N1 |
| **Console UART RX** | `PA3` | Onboard VCP | `LPUART1_RX` (AF12)| ST-LINK V3E Virtual COM Port RX @ 115200 8N1 |

---

## 3. Project Structure Created

```
x-nucleo-ihm07m1/
├── platformio.ini               # PlatformIO config (nucleo_g474re, stm32cube, OpenOCD ST-Link)
├── .gitignore                   # Ignores .pio, .vscode, build binaries
├── include/
│   ├── main.h                   # Global pinout definitions, clock definitions, peripheral handles
│   ├── stm32g4xx_hal_conf.h     # HAL configuration (GPIO, TIM, LPUART, ADC, PWR, RCC)
│   ├── ihm07m1.h                # Driver API for L6230, PWM configuration, ADC channels
│   ├── motor_control.h          # Multi-mode state machine, open-loop V/f, 6-step, telemetry
│   ├── console.h                # LPUART1 non-blocking driver & dashboard renderer prototypes
│   └── printf.h                 # Lightweight non-allocating tinyprintf
├── src/
│   ├── main.c                   # Entrypoint, 170 MHz clock setup, background loop, UI polling
│   ├── ihm07m1.c                # Hardware abstraction layer for L6230, TIM1, TIM16, ADC1
│   ├── motor_control.c          # Motor state machine, rotating voltage vector, 6-step table
│   ├── console.c                # ANSI fixed-coordinate non-scrolling dashboard & UART I/O
│   └── printf.c                 # Standalone printf implementation
└── doc/
    ├── implementation_plan.md   # Current copy of technical implementation plan
    ├── walkthrough.md           # Current copy of walkthrough document
    └── datasheets/              # Hardware reference documentation
```

---

## 4. Build & Compilation Verification

Clean build achieved using PlatformIO Core 6.2.0:
```bash
$ ~/.platformio/penv/bin/pio run
Processing nucleo_g474re (platform: ststm32; board: nucleo_g474re; framework: stm32cube)
--------------------------------------------------------------------------------
HARDWARE: STM32G474RET6 170MHz, 128KB RAM, 512KB Flash
RAM:   [          ]   0.5% (used 612 bytes from 131072 bytes)
Flash: [          ]   4.9% (used 25520 bytes from 524288 bytes)
Building .pio/build/nucleo_g474re/firmware.bin
========================= [SUCCESS] Took 5.54 seconds =========================
```

---

## 5. Live Console Non-Scrolling Dashboard Preview

When connected via ST-Link USB at 115200 baud, the terminal renders an in-place ANSI dashboard updating at 10 Hz without flicker or terminal scroll creep:

```text
================================================================================
      STM32G474RE + X-NUCLEO-IHM07M1 -- BLDC MOTOR EVALUATION DASHBOARD        
================================================================================
 Port   : LPUART1 (PA2/PA3) @ 115200 8N1   | CPU Clock: 170 MHz | PWM: 20 kHz
 Uptime : 00:02:14                         | Sample: #01340  [Heartbeat: /]
--------------------------------------------------------------------------------
 POWER STAGE TELEMETRY (X-NUCLEO-IHM07M1)
--------------------------------------------------------------------------------
 Bus Voltage (VBUS)         : 24.12 V [OK]
 Driver Board Temperature   : 28.4 deg C
 Speed Potentiometer (R42)  :  35.2 %
 Driver Status (L6230 DIAG) : [READY / NORMAL]
 Hall Sensor Signals (H321) : State 5 [H1=1, H2=0, H3=1]
--------------------------------------------------------------------------------
 MOTOR DRIVE STATUS
--------------------------------------------------------------------------------
 Drive State                : RUNNING
 Control Strategy           : STAGE 1: OPEN-LOOP V/F CHAR
 Target / Estimated Speed   : Target  600 RPM | Est  600 RPM (CW)
 Commanded Duty Cycle       :  15.0 % (ARR = 4249)
 Phase Currents (mA)        : IA= 142 mA | IB= 138 mA | IC= 145 mA [Pk= 145 mA]
 Current Trip Limit         : 1.20 A (PWM Ref on PB4)
================================================================================
 INTERACTIVE COMMANDS:                                                          
  [SPACE] Start/Stop Motor   |  [+/-] Inc/Dec Duty Cycle  |  [D] Toggle Direction
  [M] Toggle Control Mode    |  [B] Active Brake          |  [R] Reset Fault     
================================================================================
```

---

## 6. Hardware Setup & Operating Cautions

> [!CAUTION]
> **Jumper J9 Setting**: Ensure jumper **J9 on the X-NUCLEO-IHM07M1 is OPEN** before applying motor supply voltage to J1 (8 V to 48 V DC). If J9 is closed while applying $>12\text{ V}$ to J1, the 5 V regulator on the NUCLEO-G474RE will be damaged.
>
> **Default Jumper Configurations**:
> - **J9**: OPEN (Separate logic USB supply and motor power).
> - **J5 & J6**: Position 2-3 CLOSED (3-shunt current sensing mode).
> - **JP3**: CLOSED (Pull-up resistors enabled for Hall sensors).
