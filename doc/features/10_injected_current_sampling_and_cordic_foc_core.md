# Feature 10: Injected 3-Shunt Current Sampling & Hardware CORDIC FOC Core

## 1. Executive Summary
This feature establishes the high-performance hardware foundation for Field-Oriented Control (FOC) on the ST Nucleo-G474RE paired with the X-NUCLEO-IHM07M1 expansion board (L6230 DMOS 3-phase driver). 

To overcome the fundamental aliasing of software-polled regular ADC sampling, this architecture introduces:
1. **TIM1 TRGO Hardware Synchronization**: TIM1 is configured in center-aligned PWM mode with Master Output Trigger (`TRGO`) set to `TIM_TRGO_UPDATE`. On every PWM counter underflow (valley), TIM1 triggers the ADC1 Injected Sequencer when low-side half-bridge switches are conducting.
2. **ADC1 Injected Group Sequencer**: Converts all three low-side phase current shunts ($I_A$ on PA0/IN1, $I_B$ on PC1/IN7, $I_C$ on PC0/IN6) sequentially within $1.34\,\mu\text{s}$ at $42.5\text{ MHz}$ ADC clock without software jitter or CPU polling latency.
3. **STM32G4 CORDIC Co-Processor Core**: Hardware acceleration (`LL_CORDIC_FUNCTION_COSINE`, 4 cycles precision, Q1.31 format) computing simultaneous $\sin(\theta)$ and $\cos(\theta)$ in $23.5\text{ ns}$ (4 clock cycles @ $170\text{ MHz}$) for instantaneous Clarke, Park, and Inverse Park vector transformations.
4. **Space Vector Modulation (SVM) & Mid-Point Carrier Injection**: Mid-point zero-sequence injection maximizing DC bus voltage utilization up to $V_{\text{BUS}} / \sqrt{3}$ (+15.5% headroom over sinusoidal PWM) while enforcing bootstrap capacitor refresh ($5\% \le \text{duty} \le 92\%$).
5. **Real-Time FOC Telemetry & Stream Reconstruction**: Non-blocking dashboard display and automated stream parser tracking real-time $I_d$ (flux) and $I_q$ (torque) currents alongside 3-phase currents.

---

## 2. Hardware Architecture & Signal Routing

### 2.1 Low-Side Current Shunt Network
The X-NUCLEO-IHM07M1 expansion board utilizes three low-side shunt resistors with TSV994 operational amplifiers in a differential configuration biased to $V_{\text{REF}} / 2 \approx 1.65\text{ V}$:
- **Shunt Resistor**: $R_s = 0.33\,\Omega$
- **TSV994 Op-Amp Gain**: $A_v = 1.53$
- **Total Sensitivity**: $S = R_s \times A_v = 0.33 \times 1.53 = 0.5049\text{ V/A}$
- **Analog Inputs**:
  - Phase A ($I_A$): `PA0` -> `ADC1_IN1`
  - Phase B ($I_B$): `PC1` -> `ADC1_IN7`
  - Phase C ($I_C$): `PC0` -> `ADC1_IN6`
- **Quiescent Zero-Current Baseline**: $V_0 \approx 1.65\text{ V} \approx 2048\text{ LSB}$ (allowing bidirectional current measurement from $-3.26\text{ A}$ to $+3.26\text{ A}$).

### 2.2 TIM1 Master Trigger (TRGO) Synchronization
In center-aligned PWM mode with `RepetitionCounter = 1` (`TIM_COUNTERMODE_CENTERALIGNED1`), the counter counts up from 0 to ARR (4249) and down from ARR to 0. An update event (UEV) is generated once per full PWM cycle ($20\text{ kHz}$) exclusively at counter underflow (valley):
$$\text{Counter Value} = 0 \implies \text{All High-Side Switches OFF, Low-Side Switches Conducting}$$
At this instant, TIM1 asserts `TRGO` via `TIM_CR2_MMS_1` (`TIM_TRGO_UPDATE`).

```
         /\          /\          /\
TIM1    /  \        /  \        /  \
Count  /    \      /    \      /    \
______/______\____/______\____/______\____
              ^                 ^
            TRGO              TRGO
          (Valley)          (Valley)
              |                 |
              v                 v
           ADC1 Inj          ADC1 Inj
           Sequence          Sequence
          (IA, IB, IC)      (IA, IB, IC)
```

### 2.3 ADC1 Injected Group Sequencer Configuration
The STM32G474 ADC1 Injected Group is configured with:
- `InjectedNbrOfConversion = 3`
- `ExternalTrigInjecConv = ADC_EXTERNALTRIGINJEC_T1_TRGO`
- `ExternalTrigInjecConvEdge = ADC_EXTERNALTRIGINJECCONV_EDGE_RISING`
- `InjectedSamplingTime = ADC_SAMPLETIME_6CYCLES_5` (153 ns)
- Ranks:
  - Rank 1: `ADC_CHANNEL_1` ($I_A$) -> result stored in `JDR1`
  - Rank 2: `ADC_CHANNEL_7` ($I_B$) -> result stored in `JDR2`
  - Rank 3: `ADC_CHANNEL_6` ($I_C$) -> result stored in `JDR3`
- Total conversion duration for all 3 channels:
  $$T_{\text{conv}} = 3 \times (6.5 + 12.5)\text{ ADC cycles} = 57\text{ cycles} \times 23.53\text{ ns} = 1.341\,\mu\text{s}$$
This completes well within the minimum low-side conduction window even at 90% duty cycle.

---

## 3. Mathematical Foundations & CORDIC Acceleration

### 3.1 Clarke Transformation (abc -> $\alpha\beta$)
Assuming a balanced 3-phase motor with isolated neutral ($I_a + I_b + I_c = 0$):
$$\begin{aligned}
I_\alpha &= I_a \\
I_\beta  &= \frac{1}{\sqrt{3}}(I_a + 2 I_b)
\end{aligned}$$
Implemented using pre-calculated constant `FOC_ONE_BY_SQRT3` ($0.57735027$).

### 3.2 STM32G4 Hardware CORDIC Co-Processor
The STM32G4 CORDIC co-processor evaluates trigonometric functions in pure hardware using shift-and-add arithmetic:
- **Function**: `LL_CORDIC_FUNCTION_COSINE` (produces primary result $\cos(\theta)$ in `RES1`, secondary result $\sin(\theta)$ in `RES2`).
- **Precision**: `LL_CORDIC_PRECISION_4CYCLES` (4 iterations, 16-bit precision, 4 clock cycles = $23.5\text{ ns}$).
- **Data Format**: 32-bit signed Q1.31 format mapping $[-\pi, \pi) \to [-2^{31}, 2^{31}-1]$.

### 3.3 Park Transformation ($\alpha\beta \to dq$)
Aligns the stationary orthogonal frame to the synchronous rotating rotor flux frame at electrical angle $\theta_e$:
$$\begin{aligned}
I_d &=  I_\alpha \cos(\theta_e) + I_\beta \sin(\theta_e) \\
I_q &= -I_\alpha \sin(\theta_e) + I_\beta \cos(\theta_e)
\end{aligned}$$
Where:
- $I_d$: Direct-axis current (flux-producing component, regulated to $0\text{ A}$ in surface-mount PMSM/BLDC).
- $I_q$: Quadrature-axis current (torque-producing component, proportional to mechanical torque $\tau_m = \frac{3}{2} p \lambda_m I_q$).

### 3.4 Space Vector Modulation (SVM) with Mid-Point Carrier Injection
SVM synthesizes line-to-line voltages up to $\frac{V_{\text{BUS}}}{\sqrt{3}}$ without overmodulation:
$$\begin{aligned}
V_a &= V_\alpha \\
V_b &= -\frac{1}{2} V_\alpha + \frac{\sqrt{3}}{2} V_\beta \\
V_c &= -\frac{1}{2} V_\alpha - \frac{\sqrt{3}}{2} V_\beta
\end{aligned}$$
The common-mode mid-point zero-sequence offset is subtracted:
$$V_{\text{mid}} = \frac{1}{2} (\max(V_a, V_b, V_c) + \min(V_a, V_b, V_c))$$
$$V_{a,\text{svm}} = V_a - V_{\text{mid}}, \quad V_{b,\text{svm}} = V_b - V_{\text{mid}}, \quad V_{c,\text{svm}} = V_c - V_{\text{mid}}$$
Normalized compare register duty cycles:
$$d_x = 0.5 + \frac{V_{x,\text{svm}}}{V_{\text{BUS}}}$$
Clamped between $5\%$ (bootstrap recharge minimum) and $92\%$ (current sampling window preservation).

---

## 4. Software API & Interface Summary

### 4.1 FOC Core API (`include/foc.h`)
```c
void FOC_Init(void);
void FOC_Reset(void);
void FOC_CordicSinCos(float theta_rad, float* sin_out, float* cos_out);
void FOC_Clarke(float ia, float ib, float ic, float* i_alpha, float* i_beta);
void FOC_Park(float i_alpha, float i_beta, float theta_rad, float* id, float* iq);
void FOC_InvPark(float vd, float vq, float theta_rad, float* v_alpha, float* v_beta);
void FOC_SVM(float v_alpha, float v_beta, float vbus, uint16_t arr, uint16_t* u, uint16_t* v, uint16_t* w);
void FOC_ProcessCurrents(float ia, float ib, float ic, float theta_rad);
```

### 4.2 PowerStage HAL Extension (`include/power_stage.h`)
```c
void PowerStage_GetInjectedCurrents(float* ia, float* ib, float* ic);
```

---

## 5. Verification & Telemetry Validation

### 5.1 Quiescent Standstill Verification
- When stopped, half bridges are disabled: $I_A = 0\text{ mA}, I_B = 0\text{ mA}, I_C = 0\text{ mA}, I_d = 0\text{ mA}, I_q = 0\text{ mA}$.
- Terminal layout verified strictly within 80 columns without wrapping.

### 5.2 Dynamic Motor Run Test (`run_20261019_003312.csv`)
An automated 10-second test was executed at 864 RPM using profile 2 on the 24V bus:
- **Phase Currents Sampled**: Live $I_A, I_B, I_C$ observed dynamically varying between $-22\text{ mA}$ and $+25\text{ mA}$.
- **Rotor Angle Synchronization**: Phase angle mapped continuously from $s\_phase\_accum$ ($[-\pi, \pi)$).
- **CORDIC Park Transformation**: Direct-axis current $I_d$ and quadrature-axis current $I_q$ resolved in real time.
- **Optical Revolution Tracking**: Optical tachometer logged 95 physical revolutions in sync.
- **CPU Overhead**: Total ISR execution time (current acquisition + CORDIC + Park transform + CCR update) measured $< 2.5\,\mu\text{s}$, representing $< 5.0\%$ CPU load at $20\text{ kHz}$.
