# Feature 18: Analog Input I/O Pin Allocation & Feasibility Analysis

## 1. Executive Summary

This document evaluates the hardware and firmware feasibility of adding up to **6 additional analog inputs** to the **NUCLEO-G474RE** development board paired with the **X-NUCLEO-IHM07M1** motor driver shield.

### Feasibility Verdict
- **5 Analog Inputs Out-of-the-Box**: There are **5 completely unallocated, physically isolated, and ADC-capable GPIO pins** routed to the ST Morpho and Arduino connectors that have **zero hardware connection to the X-NUCLEO-IHM07M1 shield** and **zero firmware allocation**.
- **6th Analog Input Options**:
  1. **Option A (Software / Operational)**: Repurpose **`PB1`** (`ADC1_IN12` / `ADC3_IN1` on Morpho `CN10 pin 24`). `PB1` is currently wired to the shield's 100 k$\Omega$ manual speed trimmer. When using SCPI, UART console, or programmatic velocity ramps, this analog channel is readily available.
  2. **Option B (Single Resistor Desolder on Shield)**: Remove **`R59` (0 $\Omega$)** on the shield to liberate **`PC3`** (`ADC12_IN9` on Morpho `CN7 pin 37`). In Field-Oriented Control (FOC) and sinusoidal commutation, the 6-step trapezoidal Back-EMF (BEMF) zero-crossing detection network is completely unused. Removing $R_{59}$ disconnects the phase 1 switching voltage, providing a clean, dedicated 6th analog input without affecting motor drive capabilities.

---

## 2. Hardware & Architecture Context

- **Target MCU**: STMicroelectronics **STM32G474RET6** (LQFP64 package, 64 pins).
- **Core Clock**: 170 MHz AHB/SYSCLK driven from HSI (16 MHz) via PLL.
- **Onboard Peripherals**: 5$\times$ 12-bit SAR ADCs (ADC1–ADC5) with up to 4 Msps conversion speed.
- **Carrier Board**: **NUCLEO-G474RE (MB1367)** featuring ST Morpho extension headers (`CN7`, `CN10`) and Arduino Uno V3 headers (`CN5`, `CN6`, `CN8`, `CN9`).
- **Power Shield**: **X-NUCLEO-IHM07M1** three-phase BLDC/PMSM driver based on the ST L6230 DMOS bridge.

---

## 3. Comprehensive Pin Audit (STM32G474RE LQFP64)

The STM32G474RE in the 64-pin package provides 51 general-purpose I/O pins across Ports A, B, C, D (PD2 only), and F (PF0, PF1). Out of these, **25 pins** have hardware ADC routing:

| Pin | LQFP64 Pin | Board Location | ADC Channel(s) | Shield / Hardware Status | Current Project Allocation | Free for Analog Input? |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`PA4`** | Pin 18 | Morpho `CN7:32` / Arduino `A2` | **`ADC2_IN17`** | **Unconnected** ($R_{75}, R_{76}$ N.M.) | Unallocated | **YES — Plug & Play** |
| **`PB11`** | Pin 33 | Morpho `CN10:18` | **`ADC12_IN14`** | **Unconnected** (Pin 18 NC) | Unallocated (Tach Mode 2 uses `PC6`) | **YES — Plug & Play** |
| **`PB12`** | Pin 34 | Morpho `CN10:16` | **`ADC1_IN11`**, **`ADC4_IN3`** | **Unconnected** (Pin 16 NC) | Unallocated | **YES — Plug & Play** |
| **`PC4`** | Pin 22 | Morpho `CN10:34` | **`ADC2_IN5`** | **Unconnected** (Pin 34 NC) | Unallocated | **YES — Plug & Play** |
| **`PC5`** | Pin 23 | Morpho `CN10:6` | **`ADC2_IN11`** | **Unconnected** (Pin 6 NC) | Unallocated | **YES — Plug & Play** |
| **`PC3`** | Pin 11 | Morpho `CN7:37` | **`ADC12_IN9`** | Connected to BEMF1 via $R_{59}$ ($0\,\Omega$) | Unallocated | **YES (Remove $R_{59}$)** |
| **`PB0`** | Pin 24 | Morpho `CN7:34` / Arduino `A3` | **`ADC1_IN15`**, **`ADC3_IN12`** | Connected to BEMF2 via $R_{60}$ ($0\,\Omega$) | Unallocated | **YES (Remove $R_{60}$)** |
| **`PA7`** | Pin 21 | Morpho `CN10:15` / Arduino `D11`| **`ADC2_IN4`** | Connected to BEMF3 via $R_{63}$ ($0\,\Omega$) | Unallocated | **YES (Remove $R_{63}$)** |
| **`PB1`** | Pin 25 | Morpho `CN10:24` | **`ADC1_IN12`**, **`ADC3_IN1`** | Onboard 100 k$\Omega$ Trimmer $R_{42}$ | Manual Speed Potentiometer | **YES (Via Software / SCPI)** |
| **`PB13`** | Pin 35 | Morpho `CN10:30` | **`ADC3_IN5`** | Tied to CURRENT REF via $R_{82}$ ($0\,\Omega$) | Unallocated in FW | No (Shared with $P_{B4}$ trip filter) |
| **`PB14`** | Pin 36 | Morpho `CN10:28` | **`ADC1_IN5`**, **`ADC4_IN4`** | Tied to DIAG/EN via $R_{49}$ ($0\,\Omega$) | Unallocated in FW | No (Shared with $P_{A6}$ BKIN fault) |
| **`PB15`** | Pin 37 | Morpho `CN10:26` | **`ADC2_IN15`**, **`ADC4_IN5`** | Tied to BEMF3 via $R_{66}$ ($0\,\Omega$) | Unallocated in FW | No (Tied to $P_{A7}$ BEMF network) |
| **`PA0`** | Pin 12 | Morpho `CN7:28` / Arduino `A0` | **`ADC12_IN1`** | Phase A Current Shunt (TSV994) | `ADC1_IN1` (Current Sense) | **NO (Dedicated)** |
| **`PA1`** | Pin 13 | Morpho `CN7:30` / Arduino `A1` | **`ADC12_IN2`** | Bus Voltage Divider ($19.15:1$) | `ADC1_IN2` (VBUS Sense) | **NO (Dedicated)** |
| **`PC1`** | Pin 9 | Morpho `CN7:36` / Arduino `A4` | **`ADC12_IN7`** | Phase B Current Shunt (TSV994) | `ADC1_IN7` (Current Sense) | **NO (Dedicated)** |
| **`PC0`** | Pin 8 | Morpho `CN7:38` / Arduino `A5` | **`ADC12_IN6`** | Phase C Current Shunt (TSV994) | `ADC1_IN6` (Current Sense) | **NO (Dedicated)** |
| **`PC2`** | Pin 10 | Morpho `CN7:35` | **`ADC12_IN8`** | L6230 Temperature Sensor (NTC) | `ADC1_IN8` (Temp Sense) | **NO (Dedicated)** |
| **`PA2`** | Pin 14 | Morpho `CN10:35` / VCP | **`ADC1_IN3`** | ST-LINK V3E Virtual COM TX | Console LPUART1_TX | **NO (Dedicated)** |
| **`PA3`** | Pin 17 | Morpho `CN10:37` / VCP | **`ADC1_IN4`** | ST-LINK V3E Virtual COM RX | Console LPUART1_RX | **NO (Dedicated)** |
| **`PA5`** | Pin 19 | Morpho `CN10:11` / Arduino `D13`| **`ADC2_IN13`** | Nucleo User Green LED (LD2) | Heartbeat Indicator | **NO (Dedicated)** |
| **`PA6`** | Pin 20 | Morpho `CN10:13` / Arduino `D12`| **`ADC2_IN3`** | L6230 Fault Line (Open-Drain) | `TIM1_BKIN` (Emergency Stop)| **NO (Dedicated)** |
| **`PA8`** | Pin 42 | Morpho `CN10:23` / Arduino `D7` | **`ADC5_IN1`** | L6230 IN1 Phase 1 Gate Input | `TIM1_CH1` (PWM Phase U) | **NO (Dedicated)** |
| **`PA9`** | Pin 43 | Morpho `CN10:21` / Arduino `D8` | **`ADC5_IN2`** | L6230 IN2 Phase 2 Gate Input | `TIM1_CH2` (PWM Phase V) | **NO (Dedicated)** |
| **`PB2`** | Pin 26 | Morpho `CN10:22` | **`ADC2_IN12`** | Shield Red User LED (D11) | User / Fault LED Indicator | **NO (Dedicated)** |
| **`PF1`** | Pin 6 | Morpho `CN7:31` | **`ADC2_IN10`** | 24 MHz HSE Crystal via SB25/SB26 | High-Speed External Clock | **NO (Dedicated)** |

---

## 4. Recommended 6-Channel Allocation Plan

To preserve the deterministic execution of the 20 kHz injected current sampling loop on **ADC1**, the 6 auxiliary analog inputs should be hosted primarily on **ADC2** and **ADC4** (or ADC3).

### Recommended Pinout Table

| Channel | MCU Pin | Header Pin | ADC Mapping | Electrical Profile | Rationale |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **AIN 1** | **`PA4`** | **Arduino A2** (CN7:32) | `ADC2_IN17` | High-Z Analog In | Available on female Arduino header; shield resistors $R_{75}, R_{76}$ are factory N.M. |
| **AIN 2** | **`PC4`** | **Morpho CN10:34** | `ADC2_IN5` | High-Z Analog In | Completely isolated pin, adjacent to AGND (`CN10 pin 32`). |
| **AIN 3** | **`PC5`** | **Morpho CN10:6** | `ADC2_IN11` | High-Z Analog In | Completely isolated pin, adjacent to Morpho GND. |
| **AIN 4** | **`PB11`** | **Morpho CN10:18** | `ADC2_IN14` | High-Z Analog In | Completely isolated pin, adjacent to Morpho GND (`CN10 pin 20`). |
| **AIN 5** | **`PB12`** | **Morpho CN10:16** | `ADC4_IN3` (or `ADC1_IN11`) | High-Z Analog In | Completely isolated pin, on secondary ADC instance (`ADC4`). |
| **AIN 6** | **`PC3`** *(mod)*<br>or **`PB1`** *(no-mod)* | **Morpho CN7:37**<br>or **CN10:24** | `ADC2_IN9`<br>or `ADC3_IN1` | High-Z Analog In<br>(or Potentiometer) | **Option A**: Remove $R_{59}$ ($0\,\Omega$) to liberate $P_{C3}$.<br>**Option B**: Use $P_{B1}$ via software if trimmer is not used. |

---

## 5. Architectural Advantages & Isolation Strategy

1. **Zero Impact on FOC Sampling Loop**:
   - `ADC1` remains dedicated to high-priority injected phase current sampling ($I_A, I_B, I_C$) triggered synchronously by `TIM1_TRGO` at PWM center-alignment.
   - Secondary ADCs (`ADC2` and `ADC4`) handle the 6 analog inputs via continuous DMA or low-priority background timer triggers (e.g. `TIM6` or `TIM7` at 1 kHz).
2. **Ground References**:
   - `CN10 Pin 32` provides clean Analog Ground (`AGND`) directly adjacent to `CN10 Pin 34` (`PC4`).
   - `CN10 Pin 20` and `CN7 Pin 8/19/20/22` provide system logic grounds adjacent to the measurement pins.
3. **No Modification Required for 5 Channels**:
   - If only 5 analog inputs are required immediately, no hardware alterations of any kind are necessary.

---

## 6. Potentiometer Re-Routing & 7-Channel Analog Architecture

### 6.1 Onboard Potentiometer Circuit Topology
On the X-NUCLEO-IHM07M1 expansion shield:
- Trimmer potentiometer **$R_{42}$ (100 k$\Omega$)** is connected between the `3V3` logic rail and `GND`.
- The wiper terminal produces net **`SPEED`**, which connects directly to **$R_{78}$ (0 $\Omega$ SMD resistor)**.
- The other terminal of $R_{78}$ connects to **`PB1`** (ST Morpho header **`CN10 Pin 24`**).
- There is no buffer op-amp; the wiper is a passive resistive voltage divider ($0\text{ to }3.3\text{ V}$).

### 6.2 Feasibility of Wiring the Potentiometer to Another Pin

Yes, the potentiometer can be wired to another analog input, but the physical and electrical implementation depends on whether you are using the **onboard trimmer** or an **external potentiometer**:

#### Approach 1: The "Zero-Bodge" 7-Channel Architecture (Recommended)
Rather than physically modifying the potentiometer traces, leave the onboard trimmer $R_{42}$ connected to **`PB1`** (`CN10 Pin 24`). Then, route the 6 accelerometer channels to the remaining 6 pins:
- **Accelerometers (6 Channels)**:
  - $X_1$: `PA4` (Arduino `A2` / `CN7 Pin 32`, `ADC2_IN17`)
  - $Y_1$: `PC4` (`CN10 Pin 34`, `ADC2_IN5`)
  - $Z_1$: `PC5` (`CN10 Pin 6`, `ADC2_IN11`)
  - $X_2$: `PB11` (`CN10 Pin 18`, `ADC2_IN14`)
  - $Y_2$: `PB12` (`CN10 Pin 16`, `ADC4_IN3` or `ADC1_IN11`)
  - $Z_2$: **`PC3`** (`CN7 Pin 37`, `ADC2_IN9` or `ADC1_IN9`) with $R_{59}$ ($0\,\Omega$) desoldered.
- **Potentiometer (7th Channel)**:
  - Stays on **`PB1`** (`CN10 Pin 24`, `ADC1_IN12` / `ADC3_IN1`).
- **Advantage**: Requires modifying only **one 0 $\Omega$ resistor ($R_{59}$)** on the shield. Zero flying wires, zero solder cuts on the trimmer. All 7 analog inputs run concurrently.

#### Approach 2: Re-Wiring the Onboard Trimmer to an Alternate Pin
If `PB1` (`CN10 Pin 24`) is specifically desired for an accelerometer channel:
1. **Disconnect $P_{B1}$**: Desolder/remove **$R_{78}$ (0 $\Omega$)** on the shield. This completely disconnects the trimmer wiper from $P_{B1}$, leaving `CN10 Pin 24` as a pure, high-impedance external input pin.
2. **Fly Jumper Wire**: Solder a thin 30 AWG jumper wire from the wiper pad of $R_{42}$ (or the trimmer side pad of $R_{78}$) to an available analog pin on the header:
   - To **`PC3`** (Morpho `CN7 Pin 37`): Requires removing $R_{59}$ ($0\,\Omega$) to isolate BEMF1.
   - To **`PB0`** (Arduino `A3` / Morpho `CN7 Pin 34`): Requires removing $R_{60}$ ($0\,\Omega$) to isolate BEMF2.
   - To **`PA7`** (Arduino `D11` / Morpho `CN10 Pin 15`): Requires removing $R_{63}$ ($0\,\Omega$) to isolate BEMF3.
3. **Trade-off**: Requires desoldering two surface-mount resistors ($R_{78}$ plus $R_{59}$ or $R_{60}$) and soldering a flying bodge wire.

#### Approach 3: External Remote Potentiometer (Panel Knob)
If a panel-mounted rotary potentiometer is desired for manual speed adjustment while the two accelerometers take the primary pins:
- Connect the 3-wire potentiometer harness (3.3V, GND, Wiper) to:
  - **`PB0`** on **Arduino header `A3`** (Morpho `CN7 Pin 34`, `ADC1_IN15` / `ADC3_IN12`), removing $R_{60}$ ($0\,\Omega$) on the shield.
  - Or **`PC3`** on Morpho `CN7 Pin 37` (`ADC12_IN9`), removing $R_{59}$ ($0\,\Omega$).
- **Advantage**: The female Arduino/Morpho header provides direct screw-terminal or breadboard-jumper access without touching the onboard trimmer.

#### Approach 4: Digital Quadrature Rotary Encoder (Eliminating Analog Drift)
Instead of consuming an analog ADC channel for manual speed control:
- Connect an incremental rotary encoder (with push-button) to **`PB8` / `PB9`** (Arduino `D14` / `D15`) or **`PB6` / `PB7`** (Arduino `D10` / Morpho `CN7:21`).
- These pins have **zero shield connections** and support hardware timer encoder interface mode (`TIM4`).
- **Advantage**: Preserves all 6 analog channels strictly for vibration sensing, eliminates analog potentiometer ADC noise/drift, and provides digital detented RPM step adjustment.

