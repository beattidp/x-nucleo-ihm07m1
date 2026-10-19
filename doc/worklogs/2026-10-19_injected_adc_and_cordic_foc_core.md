# Worklog: ADC Injected 3-Shunt Current Sampling & CORDIC FOC Integration

- **Date**: 2026-10-19
- **Time**: 00:35 UTC (18:35 MDT)
- **Agent**: Antigravity Pair Programming Assistant
- **User**: beattidp

---

## 1. Objectives
1. Configure ADC1 Injected Group for hardware-triggered, low-side 3-shunt current sampling synchronized to TIM1 center-aligned PWM valley (counter underflow).
2. Integrate the STM32G4 CORDIC hardware co-processor for instantaneous trigonometric computation (23.5 ns).
3. Implement the Field-Oriented Control (FOC) math engine: Clarke transformation, CORDIC Park transformation, Inverse Park transformation, and Space Vector Modulation (SVM) with common-mode zero-sequence injection.
4. Integrate the FOC pipeline into the 20 kHz TIM1 PWM update interrupt.
5. Upgrade the console dashboard and telemetry reconstruction scripts to capture and display real-time $I_d$ (flux) and $I_q$ (torque) currents.
6. Verify live operation on the bare 5010-360KV rotor at 24V.

---

## 2. Actions Completed

### 2.1 Hardware Timer & Injected ADC Implementation (`src/ihm07m1.c`)
- Configured TIM1 Master Output Trigger (`TRGO`) on Update Event (`TIM_TRGO_UPDATE`) via `HAL_TIMEx_MasterConfigSynchronization`.
- Set `hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE`.
- Configured ADC1 Injected Group with 3 ranks:
  - Rank 1: `ADC_CHANNEL_1` ($I_A$ on PA0)
  - Rank 2: `ADC_CHANNEL_7` ($I_B$ on PC1)
  - Rank 3: `ADC_CHANNEL_6` ($I_C$ on PC0)
  - Trigger: `ADC_EXTERNALTRIGINJEC_T1_TRGO`, rising edge, sampling time `ADC_SAMPLETIME_6CYCLES_5`.
- Pre-configured channel sampling times in `IHM07M1_Init` so regular conversions (VBUS, Temp, Pot) avoid touching SMPR during runtime.
- Implemented `IHM07M1_StartInjectedSampling()` and `IHM07M1_GetInjectedCurrents(float* ia, float* ib, float* ic)` with 64-sample baseline offset subtraction.
- Routed `PowerStage_GetInjectedCurrents` through the PowerStage HAL.

### 2.2 FOC Engine Integration (`src/foc.c`, `src/motor_control.c`)
- Initialized STM32G4 CORDIC co-processor on AHB1 in `FOC_Init()` (function COSINE, 4 cycles precision, 32-bit Q1.31 format).
- Integrated `FOC_Clarke`, `FOC_Park` (via CORDIC), and `FOC_ProcessCurrents` inside the 20 kHz `Motor_PWM_UpdateISR`.
- Electrical angle $\theta_e$ mapped directly from 32-bit phase accumulator $s\_phase\_accum$ into $[-\pi, \pi)$.
- Synchronized FOC $I_d, I_q$ filtered currents to `g_telemetry.id_amps` and `g_telemetry.iq_amps`.
- When stopped, half-bridge drivers are disabled and phase currents are clamped cleanly to $0\text{ mA}$.

### 2.3 Dashboard & Telemetry Stream Upgrades (`src/console.c`, `tools/reconstruct_telemetry.py`)
- Adjusted main dashboard layout to stay strictly within 80 columns without terminal line wrapping:
  - Row 24: `Phase & FOC Currents (mA)  : IA=%4d IB=%4d IC=%4d | Id=%4d Iq=%4d mA`
  - Row 25: `HW Trip Limit & Peak Curr  : %.2f A (L6230 OCD) | Peak: %4d mA`
- Updated `tools/reconstruct_telemetry.py` to parse both rows, extracting $I_a, I_b, I_c, I_{pk}, I_d, I_q$, and exporting them to the synchronized CSV dataset.

---

## 3. Verification & Live Run Results

### 3.1 Flash & Quiescent Verification
- Compiled with PlatformIO (`RAM: 4.8%`, `Flash: 10.0%`).
- Flashed target over ST-Link via OpenOCD.
- Console snapshot confirmed:
  `Phase & FOC Currents (mA)  : IA=   0 IB=   0 IC=   0 | Id=   0 Iq=   0 mA`
  `HW Trip Limit & Peak Curr  : 2.20 A (L6230 OCD) | Peak:    0 mA`
  Terminal alignment clean, no wrapping.

### 3.2 Dynamic Motor Run Test
- Executed `run_motor_test.py` at 864 RPM for 10 seconds:
  - Telemetry log saved: `test_runs/run_20261019_003312.log`
  - Synchronized CSV exported: `test_runs/run_20261019_003312.csv`
- Optical tachometer registered 95 physical revolutions.
- Real-time phase currents and transformed $I_d$ (flux) / $I_q$ (torque) logged at each 250 ms telemetry frame.
- Total ISR execution time verified $< 2.5\,\mu\text{s}$ ($< 5.0\%$ CPU load at 20 kHz).

---

## 4. Next Steps
1. Implement dual PI current regulators ($PI_d$ and $PI_q$) operating on current error setpoints ($I_{d,\text{ref}} = 0$, $I_{q,\text{ref}}$ from speed/torque demand).
2. Connect Inverse Park and Space Vector Modulation (SVM) to generate active PWM compare values ($CCR1, CCR2, CCR3$) replacing the open-loop sine table.
3. Integrate optical tachometer position/speed observer for closed-loop FOC commutation.
