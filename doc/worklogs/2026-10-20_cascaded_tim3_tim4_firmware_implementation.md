# Worklog: Cascaded TIM3+TIM4 32-bit Optical Tachometer Implementation & Hardware Flashing

**Date**: 2026-10-20  
**Time**: 01:50 Mountain / 07:50 UTC  
**Agent**: Antigravity  
**User**: beattidp  

---

## 1. Objectives & Execution
- Implement the cascaded 16-bit timer architecture (`TIM3` Master + `TIM4` Slave) to provide a 32-bit optical tachometer timebase on an independent expansion pin, completely freeing `TIM2` for upcoming 3-phase Hall effect sensors (`H1`, `H2`, `H3` on connector `J3`).
- Select and configure the dedicated input capture pin: **PC6 on ST Morpho CN10 Pin 4** (`TIM3_CH1`).
- Implement the coherent capture reconstruction algorithm and the non-blocking double-read algorithm in `src/ihm07m1.c`.
- Build the firmware using PlatformIO and flash the target via ST-Link.
- Update live console telemetry to indicate active pin and timer configuration: `Optical Tachometer [CN10-4]`.
- Provide exact physical wiring instructions referenced directly to the user's test bench setup photograph (`doc/img/20261020.jpg`).

---

## 2. Firmware Implementation Details

### 2.1 Hardware Configuration (`include/ihm07m1.h`)
- Defined `OPTICAL_TACH_MODE = 2` (Mode 2: PC6 on Morpho CN10 Pin 4, `TIM3_CH1` + `TIM4` Slave cascaded 32-bit).
- Configured:
  - `OPTICAL_TACH_PIN`: `GPIO_PIN_6` on `GPIOC` (`GPIO_AF2_TIM3`).
  - `OPTICAL_TACH_TIM_MASTER`: `htim3` (TIM3 Channel 1, Master TRGO on Update).
  - `OPTICAL_TACH_TIM_SLAVE`: `htim4` (TIM4 in External Clock Mode 1 clocked by `TIM_TS_ITR2`).
  - `OPTICAL_TACH_PIN_LABEL`: `"CN10-4 (TIM3+4)"`.

### 2.2 Cascaded Timers & Coherent Capture in `src/ihm07m1.c`
- Initialized `TIM4` as Slave:
  - APB1 clock, prescaler = 0, ARR = 0xFFFF.
  - `SlaveMode = TIM_SLAVEMODE_EXTERNAL1`.
  - `InputTrigger = TIM_TS_ITR2` (routes `tim3_trgo` to `tim4_itr2` per RM0440 Table 63).
- Initialized `TIM3` as Master:
  - Clock = 170 MHz, prescaler = 169 (1.000 MHz, 1.0 us tick resolution), ARR = 0xFFFF.
  - Master Output Trigger = `TIM_TRGO_UPDATE` on counter overflow.
  - Channel 1 Input Capture configured with hardware digital filter `0x0F` (1.5 us) on `TIM_ICPOLARITY_FALLING`.
  - Interrupt enabled on `TIM3_IRQn`.
- Coherent Capture Reconstruction:
  - On each optical edge, hardware latches `capture_lsb = TIM3->CCR1`.
  - In `TIM3_IRQHandler()`, a double-read samples live `(msb1, lsb_now)` coherently.
  - If `capture_lsb > lsb_now`, exactly one overflow occurred between the physical pulse edge and the ISR execution; the true upper word is `msb1 - 1`.
  - Otherwise, `msb1` is used.
  - Combined `capture_32 = ((uint32_t)msb_at_capture << 16) | capture_lsb` is passed to `Motor_OpticalFallingEdgeISR(capture_32)`.
- Live Reading API:
  - Implemented `IHM07M1_Tach_GetTime32()` and `PowerStage_Tach_GetTime32()` using the double-read loop (`while (high1 != high2)`).

---

## 3. Verification & Flashing Results
- **PlatformIO Compilation**: `[SUCCESS] Took 4.46 seconds`, 0 warnings, 0 errors. RAM: 5.1%, Flash: 10.8%.
- **ST-Link Upload**: `** Verified OK **`, `** Resetting Target **`, `[SUCCESS] Took 6.41 seconds`.
- **Live Console Verification**:
  - Dashboard row 11 confirms: `Optical Tachometer [CN10-4] :   0 RPM (Revs:     0) [STOPPED]`.
  - Power stage telemetry: Bus Voltage = 24.29 V [OK], Driver Status = [READY / NORMAL].
  - Motor profile: [2/4] 5010-360KV Bare Rotor.

---

## 4. Hardware Connection Guide (Referencing `doc/img/20261020.jpg`)
- In the test setup photo:
  - Board orientation: White ST-Link section on the left, blue motor shield on the right.
  - Header **CN10** is the top 2-row male header.
  - **Outer Row** (top row closest to the board edge): Even pin numbers (Pin 2, Pin 4, Pin 6, ...).
  - **Pin 1 / Pin 2 end**: The LEFT end of the header (near the ST-Link perforation).
  - **Connection Point**:
    - **Signal Wire (Blue wire currently in J3 Pin 3 / Z+)**: Move to **CN10 Pin 4 (PC6)**.
      - **CN10 Pin 4** is the **2nd pin from the left on the OUTER row**.
    - **Ground & Power**:
      - The optical sensor board's green power LED is already on and properly powered. Its ground and power connections do not need to be changed.
