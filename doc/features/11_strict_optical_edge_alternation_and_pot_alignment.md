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
- When TIM2 was configured solely for falling edge capture, ringing and inverter PWM switching noise coupled into the slow analog rising transition caused false falling edge triggers. Consequently, the tachometer captured both the start ($t=0$) and end ($t=22.8\text{ ms}$) of the optical marker, measuring the marker duration ($22.8\text{ ms} \implies 2636\text{ RPM}$) instead of the true revolution period ($144\text{ ms} \implies 416\text{ RPM}$).
- In addition, an arbitrary $80\text{ ms}$ threshold prematurely decayed estimated RPM during steady-state rotation below 750 RPM.

### Dual-Channel Hardware Implementation
STM32 TIM2 captures both edges from PB10 (TI3) using native direct and indirect channel mapping:
- **TIM2 Channel 3**: Configured as `TIM_ICSELECTION_DIRECTTI` with `TIM_ICPOLARITY_FALLING` and digital filter `0x0F` (8 timer clocks at $f_{\text{DTS}} / 32 \approx 1.5\,\mu\text{s}$).
- **TIM2 Channel 4**: Configured as `TIM_ICSELECTION_INDIRECTTI` with `TIM_ICPOLARITY_RISING` and digital filter `0x0F`.

```
PB10 (TI3) ────┬───> TIM2_CH3 (Direct TI3, Falling Edge)  ──> Motor_OpticalFallingEdgeISR()
               └───> TIM2_CH4 (Indirect TI3, Rising Edge) ──> Motor_OpticalRisingEdgeISR()
```

### Edge State Machine & Transition Blanking
Strict alternation is enforced across both hardware interrupts and software validation:
1. **Falling Edge Handler (`Motor_OpticalFallingEdgeISR`)**:
   - Only processes when in `OPTICAL_EDGE_WAIT_FALLING`.
   - **Rising-Edge Blanking**: Rejects any falling edge occurring $< 1500\,\mu\text{s}$ after a rising edge (eliminating rising transition slew bounce).
   - **Minimum Period**: Validates $\Delta t \ge 5000\,\mu\text{s}$ ($< 12,000\text{ RPM}$).
   - Advances state to `OPTICAL_EDGE_WAIT_RISING`, increments revolution count, updates delta ticks, and returns `true`.
   - Hardware disables `TIM_IT_CC3` and enables `TIM_IT_CC4`.
2. **Rising Edge Handler (`Motor_OpticalRisingEdgeISR`)**:
   - Only processes when in `OPTICAL_EDGE_WAIT_RISING`.
   - **Minimum Pulse Width**: Validates that line was LOW for at least $800\,\mu\text{s}$ (distinguishing physical optical marker passage from electrical noise impulses).
   - Records rising capture timestamp, advances state to `OPTICAL_EDGE_WAIT_FALLING`, and returns `true`.
   - Hardware disables `TIM_IT_CC4` and re-arms `TIM_IT_CC3`.
3. **Adaptive Dynamic Decay**:
   - In `Motor_Update1ms()`, decay is gated by:
     $$\text{decay\_threshold\_ms} = \max\left(120, \ \frac{3}{2} \times \frac{60000}{\text{optical\_rpm} \times \text{PULSES\_PER\_REV}}\right)$$
   - Prevents artificial sawtooth drops during steady-state rotation while providing smooth decay upon stopping.
   - Stationary timeout extended to $2000\text{ ms}$ ($< 30\text{ RPM}$), resetting edge state cleanly.

---

## 3. Verification & Live Bench Results

### Steady-State Test at 700 RPM (`test_runs/run_20261019_011342.csv`)
- **Commanded Speed**: 700 RPM
- **Measured Optical Speed**: **700.0 RPM** steady-state (Avg: 698.8 RPM)
- **Total Revolutions Counted**: 81 revs in 12 seconds
- **Evaluation**: `[PASS] MOTOR ROTATING IN SYNCHRONISM`
- Acceleration ramp tracked smoothly from 108 RPM up to 700 RPM without a single spurious spike or premature decay.
