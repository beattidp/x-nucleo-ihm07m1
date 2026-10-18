# Feature 08: Optical Tachometer Integration & Real-Time Stall Detection

## 1. Overview
The optical tachometer subsystem enables direct, non-contact measurement of physical motor shaft rotation via a photodetector mounted on the test fixture. By capturing falling-edge transitions on expansion board connector **`J3 Pin 3 (PB10 / TIM2_CH3)`**, the firmware computes exact mechanical RPM and detects rotor lockup, stall, or out-of-sync vibration.

---

## 2. Hardware Interface
- **Expansion Connector**: **`J3`** (5-pin 2.54 mm male header on X-NUCLEO-IHM07M1).
- **Ground Pin**: **`J3 Pin 5 (GND)`**.
- **Signal Pin**: **`J3 Pin 3 (Z+ / PB10)`** $\rightarrow$ Configured as `GPIO_MODE_AF_PP` with `GPIO_PULLUP` and Alternate Function `GPIO_AF1_TIM2`.
- **Optional Power**: **`J3 Pin 4 (+5V)`**.
- **Protection**: Onboard 1.8 kΩ series resistor (`R32`), dual BAT30K Schottky diodes clamped to 3.3V and GND (`D4`), and 10 pF noise filtering capacitor (`C22`).

---

## 3. Peripheral Architecture

### 3.1 TIM2 32-Bit Input Capture
- **Counter Clock**: 170 MHz system clock prescaled by $PSC = 169$ yields an exact **1.000000 MHz (1.0 µs)** timebase.
- **Auto-Reload**: `0xFFFFFFFF` (32-bit free-running mode; max period before rollover is ~71.5 minutes).
- **Channel 3 Configuration**:
  - Direct input (`TIM_ICSELECTION_DIRECTTI`).
  - Active transition: Falling edge (`TIM_ICPOLARITY_FALLING`).
  - Digital filter: `0x04` (~50 ns debounce filter to suppress 20 kHz inverter switching transients).
- **Interrupt Vector**: `TIM2_IRQHandler` at NVIC Priority 3, 0.

### 3.2 Capture ISR & Period Calculation
On each falling edge:
$$\Delta\text{ticks} = CCR3_{\text{curr}} - CCR3_{\text{prev}}$$
Using 32-bit unsigned subtraction, timer rollover is automatically resolved without branch penalties. Spurious pulses narrower than 500 µs (> 120,000 RPM) are rejected.

In the deterministic 1 ms task (`Motor_Update1ms()`):
$$\text{RPM}_{\text{raw}} = \frac{60,000,000.0}{\Delta\text{ticks}}$$
$$\text{RPM}_{\text{filt}} = \text{RPM}_{\text{filt}} \times 0.70 + \text{RPM}_{\text{raw}} \times 0.30$$

### 3.3 Zero-Speed Timeout & Dynamic Decay
If no optical pulses occur for $> 60\text{ ms}$, the measured RPM dynamically decays toward zero based on elapsed time ($60000 / \Delta t$). If no pulses are received for $> 600\text{ ms}$ (< 100 RPM), `optical_rpm` snaps to `0.0`.

---

## 4. Analytical Stall & Vibration Detection

The motor drive continuously compares commanded/open-loop rotating field speed against measured optical shaft speed:

| State | Commanded Speed | Measured Optical Speed | Slip Ratio ($\eta$) | Displayed Status Tag |
|:---|:---:|:---:|:---:|:---|
| **Stopped** | 0 RPM | 0 RPM | N/A | `[STOPPED]` |
| **Normal Rotation** | $\ge 100\text{ RPM}$ | Matches Commanded ($\pm 20\%$) | $< 0.40$ | `[ROTATING - OK]` (Green) |
| **Slipping / Vibrating**| $\ge 100\text{ RPM}$ | $< 60\%$ of Commanded | $\ge 0.40$ | `[SLIPPING / VIBRATING]` (Yellow) |
| **Stalled / Locked** | $\ge 100\text{ RPM}$ | $< 15\text{ RPM}$ for $> 800\text{ ms}$ | $1.00$ | `[STALLED / LOCKED]` (Red) |

---

## 5. Live Console Dashboard Integration
The non-scrolling ANSI terminal dashboard at 115200 baud renders:
- **Line 14**:
  `Optical Tachometer (J3-Z+) : <RPM> RPM (Revs: <count>) [STATUS]`
- **Line 21**:
  `Target / Estimated Speed : Target <tgt> | Est <openloop> | Real <optical> RPM (<dir>)`

---

## 6. Verification
- Compiled and uploaded cleanly via PlatformIO (`Flash: 44,400 bytes, RAM: 6,220 bytes`).
- Live hardware capture confirmed:
  - While stopped: Displays `[STOPPED]`.
  - While commanded to 150 RPM without rotation: Automatically detects and displays `[STALLED / LOCKED]`.
