# Worklog: Cascaded 16-bit Timer Architecture & Coherent Reading Analysis

**Date**: 2026-10-19  
**Time**: 22:15 Mountain / 04:15 UTC  
**Agent**: Antigravity  
**User**: beattidp  

---

## 1. Objectives & Inquiry
- Analyze the user's architectural guidance regarding moving the optical tachometer to an alternate pin location where the hardware timer is 16-bit.
- Evaluate the cascaded timer configuration:
  - Master Timer (LSB): Base counter counting up at 1.0 us tick resolution, generating TRGO on Update Event (overflow).
  - Slave Timer (MSB): Configured in External Clock Mode 1 clocked by Master TRGO (via ITRx internal trigger matrix).
- Address critical considerations:
  1. Internal Trigger Routing (interconnection table / ITR mappings in STM32 reference manual RM0440 Table 63).
  2. Coherent Reading (Atomicity): Overcoming race conditions across separate 16-bit registers during both live polling and asynchronous input capture.
  3. Preservation of TIM2 for future 3-phase Hall effect sensors (H1, H2, H3 on connector J3).

---

## 2. Key Architectural Findings

### 2.1 Why Alternate Pin Locations Require a 16-bit Timer
- On the STM32G474RE, only **TIM2** and **TIM5** are 32-bit timers; all other general-purpose and advanced timers are 16-bit.
- `TIM2` is hardwired on the X-NUCLEO-IHM07M1 connector J3 to:
  - Hall 1: PA15 (`TIM2_CH1`)
  - Hall 2: PB3 (`TIM2_CH2`)
  - Hall 3: PB10 (`TIM2_CH3`)
- In STM32 Hardware Hall Sensor Interface mode, `TIM2->CNT` is automatically reset on every Hall transition (every 60 electrical degrees). Therefore, `TIM2_CH4` (PB11) cannot simultaneously measure the optical tachometer's 1-pulse-per-revolution period.
- `TIM5` channels on the 64-pin package collide with critical motor shield signals (PA0/ADC_CurrA, PA1/ADC_VBUS, PA2/TX, PA3/RX, PC12/EN3).
- Thus, any alternate pin location outside TIM2 must utilize a 16-bit timer.

### 2.2 Cascaded Pair Selection & Internal Trigger Routing
- From RM0440 Table 63 ('From timer to timer'):
  - `TIM3` TRGO routes directly to `TIM4` `tim4_itr2` (`TIM_TS_ITR2`).
  - Symmetrically, `TIM4` TRGO routes directly to `TIM3` `tim3_itr3` (`TIM_TS_ITR3`).
- Both `TIM3` and `TIM4` are general-purpose 16-bit timers on APB1, currently completely unallocated in the firmware.
- Master selection: **TIM3 as Master (LSB)**, clocked at 1.0 MHz (prescaler = 169) with period 0xFFFF. Master TRGO = Update Event.
- Slave selection: **TIM4 as Slave (MSB)**, configured in `TIM_SLAVEMODE_EXTERNAL1` clocked by `TIM_TS_ITR2` (TIM3 TRGO) with period 0xFFFF.
- Physical Pin Selection: **PC6 on Morpho CN10 Pin 4** (`TIM3_CH1`). PC6 is completely open/unconnected on the X-NUCLEO-IHM07M1 shield and directly adjacent to GND.

### 2.3 Coherent Reading & Mathematical Atomicity Proof
- **Live Reading (Double-Read)**:
  `high1 = TIM4->CNT; low = TIM3->CNT; high2 = TIM4->CNT; while (high1 != high2)`.
- **Capture Timestamping (Disambiguation)**:
  When an optical pulse arrives, `capture_lsb` is captured by hardware into `TIM3->CCR1`.
  In the ISR, a double-read captures `lsb_now` and `msb1`.
  Because ISR latency is under 50 us (far below the 65.5 ms rollover interval), at most one rollover can occur.
  If `capture_lsb > lsb_now`, exactly one rollover occurred between the edge and the ISR read, meaning `msb_at_capture = msb1 - 1`.
  Otherwise, `msb_at_capture = msb1`.
  Combined: `capture_32 = ((uint32_t)msb_at_capture << 16) | capture_lsb`.
  This guarantees 100% mathematical accuracy without any race conditions.

---

## 3. Status & Next Actions
- Documented in `doc/features/14_cascaded_16bit_timers_for_32bit_optical_tachometry.md`.
- Next step: Present findings and design to the user for confirmation and alignment before implementation.
