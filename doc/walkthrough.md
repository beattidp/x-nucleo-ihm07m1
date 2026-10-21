# Master Walkthrough: STM32G474RE + X-NUCLEO-IHM07M1 Motor Evaluation System

## Executive Summary

This walkthrough document tracks the active engineering milestones of the **STM32G474RE + X-NUCLEO-IHM07M1** (and IHM08M1) three-phase BLDC/PMSM motor evaluation firmware. 

The firmware provides a deterministic, real-time motor control core (20 kHz PWM / 1 kHz control task) supporting Voltage-Mode 6-Step commutation, 4-Quadrant Field Oriented Control (FOC) with hardware CORDIC acceleration, 32-bit cascaded optical tachometry, hardware Hall sensor decoding, and dynamic symmetrical 3-shunt offset calibration. 

The latest milestone introduces a **reusable, zero-heap embedded SCPI (Standard Commands for Programmable Instruments)** parser in [`lib/scpi/`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/lib/scpi), enabling automated host interrogation via PyVISA, Python, and GUI dashboards while coexisting seamlessly with the interactive serial console.

---

## Active Milestone: Reusable SCPI Parser & Test Fixture Automation

### 1. Reusable Engine ([`lib/scpi/`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/lib/scpi))
- **Zero Runtime Heap Allocation**: Static buffers for line reception (256 B), response formatting (256 B), and circular FIFO error queue (16 entries).
- **Transport Decoupling**: Abstract stream interface [`IScpiIo`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/lib/scpi/include/scpi/scpi_io.h) isolates the parser from physical hardware.
- **Standards Compliance**: Compliant with SCPI-99 and IEEE 488.2, featuring case-insensitive 3/4-letter short/long form matching, optional hierarchy nodes (`[:NODE]`), semicolon command chaining (`;`), and status reporting registers (`STB`, `ESR`, `ESE`, `SRE`).
- **PlatformIO Submodule-Ready Layout**: Automatically compiled as `libscpi.a` with zero external dependencies, ready to be extracted via `git subtree split` across future test fixtures.

### 2. Flywheel Test Fixture Adapter ([`src/flywheel_scpi.cpp`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/flywheel_scpi.cpp))
- **33 Subsystem Handlers**:
  - **Drive Control**: `:OUTPut[:STATe]`, `:OUTPut:BRAKe`, `:SOURce:MOTor:SPEEd`, `:SOUR:MOT:DIR`, `:SOUR:MOT:MODE`, `:SOUR:MOT:DUTY`, `:SOUR:MOT:SLEW:*`, `:SOUR:MOT:CURR:LIM`, `:SOUR:MOT:POT`, `:SOUR:MOT:PROF:SEL`.
  - **Telemetry**: `:MEAS:VOLT:DC?`, `:MEAS:TEMP?`, `:MEAS:SPEE:OPTI?`, `:MEAS:SPEE:HALL?`, `:MEAS:SPEE[:ACTUal]?`, `:MEAS:CURR:PEAK?`, `:MEAS:CURR:PHASe:ALL?`, `:MEAS:CURR:DQ?`, `:MEAS:PULSe:OPTI?`.
  - **High-Speed Snapshot Query (`:MEASure:TELEmetry:ALL?`)**: Returns all 12 channels in a single CSV line for fast host-side PyVISA polling.
  - **Diagnostics & Self-Test**: `:TEST:STALL?`, `:TEST:SLIP?`, `:TEST:OPTI:RES`, `:TEST:CAL:CURR`, `:TEST:CHAR:*`, `:FAUL:RES`, `:FAUL:STAT?`.
  - **Communication Switching**: `:SYSTem:COMMunication:MODE <CONSOLE|SCPI>`.

### 3. Protocol Coexistence & Zero-Friction Switching
- **Interactive Console Mode (`COMM_MODE_CONSOLE`)**: Active 4 Hz ANSI differential dashboard with real-time telemetry and single-keystroke interactive control.
- **SCPI Instrument Mode (`COMM_MODE_SCPI`)**: Dashboard updates are automatically muted to guarantee deterministic request-response transaction framing.
- **Transition Strategy**: Receiving standard SCPI headers (`*` or `:`) or pressing hotkey `#` automatically switches the firmware to SCPI mode. Sending `:SYST:COMM:MODE CONSOLE` restores console mode and forces a full screen redraw ([`Console_RequestFullRedraw()`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/console.c)).

---

## Verification & Validation Summary

### 1. Host C++ Unit Tests (`lib/scpi/test/`)
```bash
make -C lib/scpi/test
```
**Results:** `10/10` test cases passed under native `g++` without hardware dependencies:
- `*IDN?`, `*RST`, `*OPC`, `*OPC?`, `*STB?`, `*ESR?`, `*ESE`, `*SRE`
- Short/long form matching (`SOUR:MOT:SPEE` vs `SOURCE:MOTOR:SPEED`)
- Optional leaf nodes (`MEAS:VOLT?` vs `MEASURE:VOLTAGE:DC?`)
- Semicolon compound command parsing (`*CLS;SOUR:MOT:SPEE 750;*OPC?`)
- Parameter validation, bounds checking, and error queue overflow handling (`-350`)

### 2. PlatformIO Firmware Build
```bash
pio run
```
**Results:** Build succeeded with zero warnings and zero errors:
- **RAM**: 6.2% (8,140 / 131,072 bytes)
- **Flash**: 13.8% (72,416 / 524,288 bytes)
- **Platform**: `ststm32 @ 19.2.0`, `nucleo_g474re` (170 MHz)

### 3. Context-Aware Thought Logging
```bash
python3 .agents/skills/thought-logger/scripts/log_thoughts.py
```
**Results:** Automatically synchronized global reasoning to [`doc/ThoughtLog.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/ThoughtLog.md) and mirrored submodule-scoped turns to [`lib/scpi/doc/ThoughtLog.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/lib/scpi/doc/ThoughtLog.md).

---

## System Documentation & Feature Index

All firmware capabilities, hardware setups, and verification records are comprehensively documented:

| Feature Doc | Description |
| :--- | :--- |
| **[`00_initial_nucleo_ihm07m1_bringup.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/00_initial_nucleo_ihm07m1_bringup.md)** | Baseline hardware mapping, LPUART1 VCP pin collision resolution, SPN7 vs SPN8. |
| **[`01_fixed_point_sine_engine.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/01_fixed_point_sine_engine.md)** | 1024-entry fixed-point sinusoidal generator for open-loop rotating vector. |
| **[`02_motor_profiles_architecture.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/02_motor_profiles_architecture.md)** | Multi-profile catalog system with runtime parameter swapping. |
| **[`03_automated_motor_characterization.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/03_automated_motor_characterization.md)** | Automated multi-phase resistance measurement, stiction current, and dynamic Kv estimation. |
| **[`04_current_limiter_and_diagnostics.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm04m1/doc/features/04_current_limiter_and_diagnostics.md)** | Hardware BKIN fault trip and software continuous current limiting ceiling. |
| **[`05_power_stage_hal_and_24v_feedforward.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/05_power_stage_hal_and_24v_feedforward.md)** | Unified power stage HAL supporting IHM07M1 (L6230) and IHM08M1 (L6398) with VBUS feedforward. |
| **[`06_spwm_center_bias_and_inertia_ramp.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/06_spwm_center_bias_and_inertia_ramp.md)** | Center-biased SPWM modulation and high-inertia acceleration/deceleration S-curve ramping. |
| **[`07_optical_tachometer_and_stall_detection_strategy.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/07_optical_tachometer_and_stall_detection_strategy.md)** | Optical tachometer strategy and stall/slip detection criteria. |
| **[`08_optical_tachometer_firmware_integration.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/08_optical_tachometer_firmware_integration.md)** | Precision optical capture implementation and pulse accumulation. |
| **[`09_motor_stall_validation_and_test_automation.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/09_motor_stall_validation_and_test_automation.md)** | Automated test runner (`tools/run_motor_test.py`) and synchronized telemetry capture. |
| **[`10_injected_current_sampling_and_cordic_foc_core.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/10_injected_current_sampling_and_cordic_foc_core.md)** | Hardware CORDIC coprocessor Park/Clarke transforms and TIM1 injected ADC sampling. |
| **[`11_strict_optical_edge_alternation_and_pot_alignment.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/11_strict_optical_edge_alternation_and_pot_alignment.md)** | Optical hysteresis filtering and speed potentiometer calibration. |
| **[`12_foc_closed_loop_active_damping_and_bidirectional_deceleration.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/12_foc_closed_loop_active_damping_and_bidirectional_deceleration.md)** | Active damping current injection and 4-quadrant dynamic regenerative braking. |
| **[`13_rapid_acceleration_and_4000rpm_harmonic_decoupling.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/13_rapid_acceleration_and_4000rpm_harmonic_decoupling.md)** | Rapid acceleration tuning, 4000 RPM field weakening, and harmonic decoupling detection. |
| **[`14_cascaded_16bit_timers_for_32bit_optical_tachometry.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/14_cascaded_16bit_timers_for_32bit_optical_tachometry.md)** | Cascaded TIM3+TIM4 32-bit hardware capture eliminating low-speed 16-bit timer overflow. |
| **[`15_3phase_hardware_hall_sensor_interface_architecture.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/15_3phase_hardware_hall_sensor_interface_architecture.md)** | TIM2 3-phase XOR Hall interface decoding 6-step state transitions and Hall RPM. |
| **[`16_symmetrical_3shunt_clarke_and_dynamic_offset_calibration.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/16_symmetrical_3shunt_clarke_and_dynamic_offset_calibration.md)** | Symmetrical Clarke transform ($I_a+I_b+I_c=0$) and dynamic rest offset calibration. |
| **[`17_scpi_flywheel_integration.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/features/17_scpi_flywheel_integration.md)** | Complete Flywheel Test Fixture SCPI command dictionary, PyVISA examples, and host verification. |

### Module Technical Reference (`lib/scpi/doc/`)
- [`architecture.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/lib/scpi/doc/architecture.md): System class diagrams, memory model, and sequence diagrams.
- [`ieee488_2_compliance.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/lib/scpi/doc/ieee488_2_compliance.md): Mandatory IEEE 488.2 commands, status reporting model, and SCPI error code catalog.
- [`extensibility_guide.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/lib/scpi/doc/extensibility_guide.md): Developer guide for declaring new commands on other test benches.
- [`user_guide.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/lib/scpi/doc/user_guide.md): Operator quickstart, syntax rules, and terminal/PyVISA recipes.
- [`walkthrough.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/lib/scpi/doc/walkthrough.md): Standalone copy of the SCPI implementation walkthrough.
