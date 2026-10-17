# Development Worklog: Unified Power Stage HAL & 24V Voltage Feedforward

- **Date**: 2026-10-17
- **Time**: 15:40:00 UTC-6
- **User**: beattidp
- **Agent**: Antigravity (Google DeepMind)
- **Target Hardware**: STM32G474RE (NUCLEO-64) + X-NUCLEO-IHM07M1 (L6230 Bridge) / X-NUCLEO-IHM08M1 (L6398 Driver)
- **Active Motor**: 5010 360KV High-Torque Sensorless BLDC (14 Pole Pairs / 28 Poles) for Flywheel Balancing

---

## 1. Summary of Changes

In response to the transition from 12V to 24V supply (up to 6S / 25.2V) and preparation for evaluating larger motors using the X-NUCLEO-IHM08M1 expansion board (L6398 gate drivers + discrete power MOSFETs), this iteration implemented:
1. **Unified Power Stage Hardware Abstraction Layer (HAL)** (`include/power_stage.h`).
2. **L6398 Power Stage Driver for X-NUCLEO-IHM08M1** (`include/driver_l6398_ihm08m1.h`, `src/driver_l6398_ihm08m1.c`).
3. **Decoupled L6230 Driver Implementation** (`include/ihm07m1.h`, `src/ihm07m1.c`).
4. **Dynamic Bus Voltage Feedforward Scaling** (`src/motor_control.c`).
5. **Voltage-Adaptive Characterization Engine** (`src/motor_characterization.c`).
6. **5010 360KV Flywheel Profile Configuration** (`src/motor_profiles.c`).
7. **Telemetry and Dashboard Abstraction** (`src/console.c`, `src/main.c`).

---

## 2. Technical Details & Architecture

### PowerStage HAL (`include/power_stage.h`)
- Declared uniform interface:
  - `PowerStage_Init()`
  - `PowerStage_SetPWM(duty_u, duty_v, duty_w)`
  - `PowerStage_Enable()` / `PowerStage_Disable()`
  - `PowerStage_GetTelemetry(PowerStageTelemetry_t *telem)`
  - `PowerStage_GetCapabilities(void)`
- Preprocessor switch `#define TARGET_POWER_STAGE` with constants:
  - `BOARD_TYPE_IHM07M1_L6230 0`
  - `BOARD_TYPE_IHM08M1_L6398 1`

### 24V Bus Voltage Scaling & Safety
- **Problem**: 5010 360KV motor has low winding resistance ($0.4 \dots 0.8\ \Omega$). Doubling supply voltage doubles stator current for any given duty cycle, risking immediate overcurrent trip of the L6230 (2.2A hardware trip / 1.4A RMS continuous).
- **Solution**: Normalizing commanded duty by $V_{\text{scale}} = V_{\text{rated}} / V_{\text{BUS}}$, reducing 50% commanded duty at 12V to 25% duty at 24V, keeping terminal phase voltage and torque steady.
- Clamped minimum open-loop duty scaled down to ~2% to prevent over-excitation at high bus voltages.

### Characterization Safety at 24V
- Phase resistance measurement pulses dynamically calculate duty from target terminal voltage $V_{\text{test}} = 0.60\text{ V}$ (~2.5% duty at 24V).
- Stiction test voltage ramp: $1.20\text{ V} \to 2.40\text{ V}$ (5% to 10% duty at 24V).
- Dynamic spin tests: 50 Hz at 2.64V, 100 Hz at 3.36V.

---

## 3. Verification & Build Results

- **Compiler**: `arm-none-eabi-gcc` via PlatformIO (`pio run`).
  - Flash: 40,424 bytes (7.7% of 512KB).
  - RAM: 5,960 bytes (4.5% of 128KB).
- **Firmware Flash**: Successfully programmed via ST-Link v3 on `/dev/ttyACM0` (`pio run -t upload`).
- **Telemetry Verified**: LPUART1 @ 115200 baud streaming non-blocking ANSI dashboard.
