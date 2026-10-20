# Feature 14: Cascaded 16-bit Timers for 32-bit Optical Tachometry

## 1. Context & Motivation

On the STM32G474RE + X-NUCLEO-IHM07M1 platform:
- The Hall sensor connector **J3** is routed to:
  - **J3 Pin 1**: Hall 1 -> PA15 (`TIM2_CH1`)
  - **J3 Pin 2**: Hall 2 -> PB3 (`TIM2_CH2`)
  - **J3 Pin 3**: Hall 3 -> PB10 (`TIM2_CH3`)
- When evaluating a motor equipped with 3-phase Hall effect sensors, `TIM2` is configured in **Hardware Hall Sensor Interface Mode** (`SMCR.SMS = 0b100`, XOR of TI1, TI2, TI3). In this mode, `TIM2->CNT` is automatically cleared on every Hall commutation edge (every 60 degrees electrical).
- Consequently, `TIM2` **cannot** simultaneously maintain a continuous free-running timebase for optical tachometer period measurement on `TIM2_CH4` (PB11).
- Furthermore, on the 64-pin STM32G474RE package, the second 32-bit timer (`TIM5`) has no available input capture channels that do not conflict with required motor shield signals (PA0/ADC_IA, PA1/ADC_VBUS, PA2/TX, PA3/RX, PC12/EN3).
- Moving the optical tachometer to an independent expansion header pin requires utilizing a **16-bit timer** (such as `TIM3`, `TIM4`, or `TIM8`).
- At 1.0 us tick resolution, a standalone 16-bit timer rolls over every 65,536 us = 65.536 ms. At speeds below 915.5 RPM, a single revolution period exceeds 65.5 ms, resulting in counter wrap-around and period ambiguity.
- Cascading two 16-bit timers in hardware (Master LSB + Slave MSB) restores the full **32-bit dynamic range** (4,294.97 seconds / ~71.58 minutes at 1 us resolution) without requiring CPU interrupt intervention for counting.

---

## 2. Hardware Timer Interconnection Matrix (RM0440 Table 63)

On the STM32G4 series, timers are linked internally via the internal trigger multiplexer (`ITR0` through `ITR11`):

| Slave Timer | Input Trigger Signal | Trigger Source Assignment | HAL Define |
| :--- | :--- | :--- | :--- |
| **TIM4** | `tim4_itr0` | `tim1_trgo` | `TIM_TS_ITR0` |
| **TIM4** | `tim4_itr1` | `tim2_trgo` | `TIM_TS_ITR1` |
| **TIM4** | **`tim4_itr2`** | **`tim3_trgo`** | **`TIM_TS_ITR2`** |
| **TIM4** | `tim4_itr4` | `tim5_trgo` | `TIM_TS_ITR4` |
| **TIM4** | `tim4_itr5` | `tim8_trgo` | `TIM_TS_ITR5` |
| **TIM4** | `tim4_itr6` | `tim15_trgo` | `TIM_TS_ITR6` |
| **TIM3** | `tim3_itr0` | `tim1_trgo` | `TIM_TS_ITR0` |
| **TIM3** | `tim3_itr1` | `tim2_trgo` | `TIM_TS_ITR1` |
| **TIM3** | **`tim3_itr3`** | **`tim4_trgo`** | **`TIM_TS_ITR3`** |
| **TIM3** | `tim3_itr4` | `tim5_trgo` | `TIM_TS_ITR4` |

### Selected Master-Slave Pair:
- **Master Timer (LSB)**: **TIM3** (16-bit general-purpose timer)
  - Base clock: 170 MHz APB1 timer clock.
  - Prescaler: 169 (1.000 MHz, 1.0 us tick).
  - Period: `0xFFFF` (65,535 us).
  - Master Output Trigger: `TIM_TRGO_UPDATE` on counter overflow.
- **Slave Timer (MSB)**: **TIM4** (16-bit general-purpose timer)
  - Slave Mode: `TIM_SLAVEMODE_EXTERNAL1` (External Clock Mode 1).
  - Input Trigger: `TIM_TS_ITR2` (`tim3_trgo`).
  - Period: `0xFFFF` (65,535 overflows).
  - Each increment represents 65,536 us.
- **Input Capture Pin**:
  - **PC6** on Morpho **CN10 Pin 4** -> `TIM3_CH1` (`GPIO_AF2_TIM3`).
  - Completely unpopulated/open on X-NUCLEO-IHM07M1.
  - Adjacent to GND pins on Morpho CN10.

---

## 3. Coherent Reading & Atomicity Solutions

Because the 32-bit timestamp is partitioned across two distinct 16-bit registers on the peripheral bus, reading is non-atomic.

### 3.1 Live Time Reading (`IHM07M1_GetTimestamp32()`)
When reading the continuous 32-bit time (e.g. for elapsed timeout or diagnostic polling), an overflow of the master timer between reading low and high registers will cause a 65.5 ms error.

**Double-Read Algorithm**:
```c
uint32_t IHM07M1_Tach_GetTime32(void) {
    uint16_t high1, low, high2;
    do {
        high1 = (uint16_t)TIM4->CNT;
        low   = (uint16_t)TIM3->CNT;
        high2 = (uint16_t)TIM4->CNT;
    } while (high1 != high2);
    
    return ((uint32_t)high1 << 16) | low;
}
```
- If an overflow occurs between reading `high1` and `low`, `high2` will not equal `high1`, causing the loop to re-read.
- At most two loop iterations occur (< 100 ns).

### 3.2 Input Capture Timestamp Reconstruction (`TIM3_IRQHandler()`)
In input capture mode, the lower 16 bits are captured into `TIM3->CCR1` by hardware at the exact instant of the pulse edge ({\text{edge}}$). The interrupt service routine executes with a finite latency $\Delta t_{\text{ISR}}$ at {\text{ISR}} > t_{\text{edge}}$.

If an overflow occurs during $\Delta t_{\text{ISR}}$, `TIM4->CNT` has already incremented, but `TIM3->CCR1` holds the pre-overflow count.

**Coherent Capture Reconstruction Algorithm**:
```c
void TIM3_IRQHandler(void) {
    if (__HAL_TIM_GET_FLAG(&htim3, TIM_FLAG_CC1)) {
        /* 1. Latch captured LSB from hardware capture register */
        uint16_t capture_lsb = (uint16_t)TIM3->CCR1;

        /* 2. Coherently sample current live master and slave counters */
        uint16_t msb1, lsb_now, msb2;
        do {
            msb1    = (uint16_t)TIM4->CNT;
            lsb_now = (uint16_t)TIM3->CNT;
            msb2    = (uint16_t)TIM4->CNT;
        } while (msb1 != msb2);

        /* 3. Disambiguate rollover between capture instant and ISR execution:
         * - If capture_lsb <= lsb_now: No rollover occurred between capture and read.
         *   MSB at capture instant was msb1.
         * - If capture_lsb > lsb_now: Exactly one rollover occurred between capture and read.
         *   MSB at capture instant was msb1 - 1.
         */
        uint16_t msb_at_capture = msb1;
        if (capture_lsb > lsb_now) {
            msb_at_capture--;
        }

        uint32_t capture_32 = ((uint32_t)msb_at_capture << 16) | capture_lsb;

        /* 4. Process edge in state machine */
        Motor_OpticalFallingEdgeISR(capture_32);

        __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_CC1);
    }
}
```

### Mathematical Proof of Coherence:
1. Since the capture event occurred at or before the double-read, {\text{capture}} \le t_{\text{now}}$.
2. The maximum ISR latency $\Delta t_{\text{ISR}} < 50\,\mu\text{s} \ll 65{,}536\,\mu\text{s}$. Therefore, at most **one** overflow can elapse between {\text{capture}}$ and {\text{now}}$.
3. If no rollover occurred: $\text{TIM3}_{\text{now}} \ge \text{TIM3}_{\text{capture}} \implies \text{capture\_lsb} \le \text{lsb\_now}$. True MSB is $\text{msb}_1$.
4. If a rollover occurred: $\text{TIM3}_{\text{now}} < \text{TIM3}_{\text{capture}} \implies \text{capture\_lsb} > \text{lsb\_now}$. True MSB is $\text{msb}_1 - 1$.
5. The reconstruction is exact under all race-condition permutations.

---

## 4. Hardware Resource Allocation Comparison

| Architecture | Timer Allocation | Pin Used | Dynamic Range | CPU Overhead | Hall Sensor Coexistence |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Current (Baseline)** | `TIM2` (32-bit native) | PB10 (J3-Z+) | 32-bit (71.5 min) | Zero | **No** (Collides with Hall 3 on J3) |
| **PB11 on TIM2** | `TIM2` (32-bit native) | PB11 (CN10 Pin 18) | 32-bit (71.5 min) | Zero | **No** (TIM2 counter is reset by Hall XOR) |
| **Single 16-bit Timer** | `TIM3` (16-bit) | PC6 (CN10 Pin 4) | 16-bit (65.5 ms) | Zero | Yes, but fails below 915 RPM |
| **Software-Extended 16-bit** | `TIM3` + Update ISR | PC6 (CN10 Pin 4) | 32-bit (71.5 min) | Interrupt every 65.5 ms (15.2 Hz) | Yes |
| **Cascaded Hardware Timers (Validated)** | `TIM3` (Master) + `TIM4` (Slave) | PC6 (CN10 Pin 4) | 32-bit (71.5 min) | **Zero** counting overhead | **Yes** (100% independent of TIM2 Hall mode) |

---

## 5. Empirical Hardware Verification Results

The physical detector was relocated to Morpho **CN10 Pin 4 (`PC6`)** for signal and **CN10 Pin 9 (`GND`)** for ground, freeing connector **J3** completely (see [`doc/img/20261020T140607.jpg`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/img/20261020T140607.jpg)). Three distinct hardware verification profiles were executed on the test bench.

### 5.1 Verification Test Summary

| Test Run | Profile & Target Speeds | Total Revs | Optical RPM Result | Status | Key Finding / Behavior Verified |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Run 1** (`run_20261020_141825`) | 1728 RPM Steady Hold (8 s) | 164 revs | Exact 1728.0 RPM steady | `[PASS]` | Verified perfect synchronism and edge detection on PC6 (`TIM3_CH1`). |
| **Run 2** (`run_20261020_141850`) | 16-bit Overflow Boundary ($288 \to 864$ RPM) | 109 revs | 287.0 RPM & 864.0 RPM | `[PASS]` | Verified cascaded 16-bit timer rollovers. At 288 RPM ($T = 208{,}333\,\mu\text{s}$), the timer rolled over 3 times per pulse with zero glitch or count loss. |
| **Run 3** (`run_20261020_141917`) | 4-Stage Bidirectional Ladder ($864 \to 2880 \to 864 \to 288$ RPM) | 316 revs | Up to 2853 RPM | `[PASS]` | Verified dynamic tracking across wide dynamic range and acceleration/deceleration transitions. |

### 5.2 Low-Speed Cascaded Overflow Analysis (Run 2)
At 288 RPM:
$$\Delta t_{\text{pulse}} = \frac{60}{288} = 0.208333\text{ s} = 208{,}333\,\mu\text{s}$$
Since a 16-bit timer at $1\,\mu\text{s}$ resolution rolls over every $65{,}536\,\mu\text{s}$:
$$N_{\text{rollover}} = \left\lfloor \frac{208{,}333}{65{,}536} \right\rfloor = 3\text{ full rollovers}$$
During every single revolution at 288 RPM, `TIM3` rolled over 3 times and incremented `TIM4` via TRGO. The live double-read and ISR capture disambiguation logic reconstructed the 32-bit timestamps without dropping a single pulse or glitching the speed estimate.

### 5.3 Deceleration Dynamics (Run 3)
During rapid deceleration ($500\text{ RPM/s}$) from 864 RPM to 288 RPM, the open-loop commutated electrical ramp slewed ahead of the physical rotor inertia for one 250 ms sample window ($534\text{ RPM}$ real vs $343\text{ RPM}$ ramp, slip = 55%), correctly triggering the transient dashboard slip warning `[SLIPPING / VIBRATING]`. As the rotor settled to 288 RPM steady state, synchronism was restored (`[ROTATING - OK]`), demonstrating high sensitivity and fidelity of the tachometer telemetry.
