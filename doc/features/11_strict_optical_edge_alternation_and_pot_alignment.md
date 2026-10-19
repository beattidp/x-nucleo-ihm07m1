# Feature 11: Strict Optical Edge Alternation & Potentiometer Target Alignment

## Overview
This feature resolves two operational discrepancies identified during hardware testing on the STM32G474RE + X-NUCLEO-IHM07M1:
1. **Potentiometer Target Speed Discrepancy**: When potentiometer mode was active, the console displayed `Target 300 RPM` at 0% pot, but the motor controller unexpectedly slewed the motor target down to `100 RPM`.
2. **Optical Tachometer Erratic Readings**: Jitter, transition ringing, and false double-triggers on the single optical marker caused erratic speed spikes (e.g., 2000–3500 RPM) and premature decay.

---

## 1. Potentiometer Target Speed Alignment

### Root Cause
- In `src/console.c`, Row 11 calculated:
  $$\text{pot\_demanded\_rpm} = \text{MOTOR\_POT\_MIN\_RPM} (300.0) + \frac{\text{pot\_pct}}{100.0} \times (3000.0 - 300.0) = 300\text{ RPM at } 0\%$$
- In `src/motor_control.c` (`Motor_Init`, `Motor_ApplyProfile`, and `Motor_Update1ms`), the lower bound evaluated:
  $$\text{min\_s} = (g\_motor\_ctrl.min\_rpm > 50.0f) \ ? \ g\_motor\_ctrl.min\_rpm : \text{MOTOR\_POT\_MIN\_RPM}$$
  Profile 2 configured `.min_rpm = 100.0f`. Because $100.0\text{ f} > 50.0\text{ f}$, `min_s` evaluated to $100.0\text{ RPM}$, causing the motor target speed to slew down to 100 RPM while the console reported 300 RPM.

### Architecture & Fix
- Potentiometer user speed demand is now strictly anchored to `MOTOR_POT_MIN_RPM` ($300.0\text{ f}$) up to `MOTOR_POT_MAX_RPM` ($3000.0\text{ f}$, capped by `g_motor_ctrl.max_rpm` if lower).
- `g_telemetry.pot_target_rpm` is maintained as the single source of truth in `MotorTelemetry_t`.
- `console.c` renders `telem->pot_target_rpm` directly, ensuring 100% synchronization between Row 11 (`[ACTIVE -> Target 300 RPM]`) and Row 21 (`Target 300 RPM`).
- Keypad step control (`<`, `>`) retains the ability to manually explore lower speeds down to `min_rpm` ($100\text{ RPM}$) when potentiometer mode is toggled off (`P`).

---

## 2. Strict Optical Edge Alternation & Transition Blanking

### Root Cause
- The optical tachometer pin PB10 (J3-Z+ / Hall 3) connects to an open-collector photodetector with internal pull-up ($\approx 40\,\text{k}\Omega$).
- On falling edge (marker entering beam), the phototransistor pulls the line LOW rapidly.
- On rising edge (marker exiting beam), the $40\,\text{k}\Omega$ pullup discharges line capacitance with an RC rise time of $\approx 5\text{--}20\,\mu\text{s}$.
- An earlier dual-channel implementation enforced an $800\,\mu\text{s}$ pulse-width cutoff (`if (pulse_width < 800) return false;`). Around 3600 RPM, the physical aperture duration fell below $800\,\mu\text{s}$, causing the tachometer to reject all rising edges, permanently lock up in `WAIT_RISING`, drop all counts, and trigger false `[SLIPPING/VIBRATING]` alarms.
- Furthermore, dedicated connector J3 will be needed by the next motor to evaluate 3-phase Hall sensors (H1, H2, H3).

### Single-Channel Dynamic Polarity Architecture
Instead of dual channels with indirect timer routing, the tachometer utilizes a single 32-bit timer capture channel (`TIM2`) that dynamically flips hardware polarity inside a single interrupt handler (`TIM2_IRQHandler`):

```
                        ┌──────────────────────────────────────────────┐
                        │              TIM2_IRQHandler()               │
                        └──────────────────────┬───────────────────────┘
                                               │
                       Is State == OPTICAL_EDGE_WAIT_FALLING?
                                 /                     \
                             YES                         NO (WAIT_RISING)
                             /                             \
             ┌───────────────────────────────┐     ┌───────────────────────────────────┐
             │ 1. Read CCR (Capture timestamp)│    │ 1. Marker exited (Rising edge)    │
             │ 2. Motor_OpticalFallingEdgeISR│     │ 2. Settle pin to logic HIGH       │
             │ 3. Switch polarity to RISING  │     │ 3. Switch polarity back to FALLING│
             │ 4. State = WAIT_RISING        │     │ 4. State = WAIT_FALLING           │
             └───────────────────────────────┘     └───────────────────────────────────┘
```

1. **Revolution Detection Strictly on Falling Edge**:
   - `Motor_OpticalFallingEdgeISR(capture)` processes each revolution.
   - **Zero Minimum Pulse Width Restriction**: Allows operation at high speeds (3600–6000+ RPM) where the LOW pulse duration becomes very narrow ($< 500\,\mu\text{s}$).
   - High-frequency sanity filter ($\Delta t \ge 1000\,\mu\text{s} \implies \le 60,000\text{ RPM}$) rejects nanosecond transient spikes.
2. **In-ISR Hardware Polarity Flipping**:
   - Polarity is flipped directly using HAL macros (`TIM_RESET_CAPTUREPOLARITY` / `TIM_SET_CAPTUREPOLARITY`).
   - The channel is briefly gated via `TIM_CCxChannelCmd()` while reprogramming `CCER` to guarantee glitch-free polarity switching.
   - Any spurious interrupt flag generated during polarity reconfiguration is cleared before re-enabling capture.
3. **Rising Transition Slew Blanking**:
   - To eliminate PWM switching noise coupling into the slow open-collector rising transition, a $500\,\mu\text{s}$ blanking window is enforced after each rising edge.
   - `TIM2` hardware digital filter is configured to `0x0F` (8 clocks at $f_{\text{DTS}} / 32 \approx 1.5\,\mu\text{s}$).

---

## 3. Alternate Expansion Pin Evaluation (Hall Coexistence)

When evaluating motors equipped with 3-phase Hall sensors, connector **J3** carries all three Hall channels:
- **J3 Pin 1 (J3-A+)**: Hall 1 $\to$ `PA15` (`TIM2_CH1` / AF1)
- **J3 Pin 2 (J3-B+)**: Hall 2 $\to$ `PB3` (`TIM2_CH2` / AF1)
- **J3 Pin 3 (J3-Z+)**: Hall 3 $\to$ `PB10` (`TIM2_CH3` / AF1)

### Expansion Pin Audit & Primary Recommendation

| Pin | Expansion Header | Connected STM32 Pin | Timer Channel | 32-Bit? | Shield Conflict? | Recommendation |
|:---:|:---|:---:|:---:|:---:|:---:|:---|
| **PB11** | **Morpho CN10 Pin 18** | **PB11** | **TIM2_CH4** | **YES (32-bit)** | **NONE** | **PRIMARY RECOMMENDATION**. Coexists on TIM2 with all 3 Hall sensors (CH1, CH2, CH3) using the same 32-bit 1 MHz timebase. Adjacent to **CN10 Pin 20 (GND)**. |
| **PB6** | Morpho CN10 Pin 17 / Arduino D10 | PB6 | TIM4_CH1 | No (16-bit) | None | Good alternate on Arduino female header, but 16-bit timer overflows every 65 ms. |
| **PC7** | Morpho CN10 Pin 19 / Arduino D9 | PC7 | TIM3_CH2 | No (16-bit) | None | Secondary alternate on Arduino D9; 16-bit timer. |

### Configurable Pin Selection Switch
In `include/ihm07m1.h`:
```c
/* Optical Tachometer Input Pin Selection:
 * 0 = PB10 on J3 Pin 3 (J3-Z+ / TIM2_CH3) - Default for current sensorless setup
 * 1 = PB11 on Morpho CN10 Pin 18 (TIM2_CH4) - Dedicated pin allowing J3 to host H1, H2, H3 Hall sensors
 */
#ifndef OPTICAL_TACH_USE_CN10_PB11
#define OPTICAL_TACH_USE_CN10_PB11   0
#endif
```

---

## 4. Verification & Live Bench Results

### Steady-State 3600 RPM Evaluation (`test_runs/run_20261019_021330.csv`)
- **Commanded Speed**: **3600 RPM**
- **Steady-State Optical Speed**: **3601 RPM** (Average: **3600.2 RPM**, Error: **0.005%**)
- **Total Revolutions Counted**: **1,519 revolutions** continuously across 48 seconds without a single missed pulse or spurious trigger.
- **Synchronism Evaluation**: `[PASS] MOTOR ROTATING IN SYNCHRONISM` (100.0% `[ROTATING - OK]` across all 190 active drive frames).
- **Ramp to Steady-State Telemetry Excerpt**:
  ```csv
  uptime,state,target,est_rpm,real_rpm,revs,status
  00:01:58,STARTING,3600,3567,3564,1326,[ROTATING - OK]
  00:01:59,STARTING,3600,3580,3579,1341,[ROTATING - OK]
  00:01:59,STARTING,3600,3588,3587,1356,[ROTATING - OK]
  00:01:59,STARTING,3600,3592,3592,1371,[ROTATING - OK]
  00:01:59,STARTING,3600,3596,3595,1386,[ROTATING - OK]
  00:02:00,STARTING,3600,3599,3599,1402,[ROTATING - OK]
  00:02:00,RUNNING,3600,3600,3600,1417,[ROTATING - OK]
  00:02:00,RUNNING,3600,3600,3600,1432,[ROTATING - OK]
  00:02:00,RUNNING,3600,3600,3600,1447,[ROTATING - OK]
  00:02:01,RUNNING,3600,3600,3600,1462,[ROTATING - OK]
  00:02:01,RUNNING,3600,3600,3601,1478,[ROTATING - OK]
  ```
- **Stop & Coast**: Clean deceleration from 3601 RPM down to 3436 $\to$ 2701 $\to$ 2007 $\to$ 0 RPM.

### Multi-Speed High-Ramp Verification (`test_runs/run_20261019_020543.csv`)
- **Total Revolutions Counted**: **704 revolutions** continuously across speed steps up to 4320 RPM target.
- **Ramp Tracking**:
  - Commanded 1728 RPM: Est 552 RPM $\to$ Real Optical **426–683 RPM**
  - Commanded 2880 RPM: Est 1220 RPM $\to$ Real Optical **1188–1220 RPM**
  - Commanded 3600 RPM: Est 1868 RPM $\to$ Real Optical **1859 RPM** (Tracking error $< 0.5\%$)
  - Commanded 4320 RPM: Est 2516 RPM $\to$ Real Optical **2510 RPM** (Tracking error $< 0.24\%$)
- Zero lockups, zero count loss, and zero slip false trips.


