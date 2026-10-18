# Worklog: Optical Tachometer Hardware Pinout, Timer Allocation & Stall Detection Strategy
**Date**: 2026-10-18  
**Author**: Antigravity (AI Assistant)  
**User**: beattidp  

## Summary of Actions
- Evaluated physical hardware interfaces on the X-NUCLEO-IHM07M1 expansion board for integrating an optical photodetector generating a falling-edge revolution trigger signal.
- Audited STM32G474RE timer peripherals against active motor control tasks (TIM1 for 20 kHz 3-phase PWM, TIM16 for current trip threshold DAC PWM, ADC1 telemetry).
- Identified **TIM2** (a 32-bit hardware timer) as having zero peripheral conflict and perfect pin routing to the dedicated **J3** Hall/Encoder header on the X-NUCLEO-IHM07M1 board.
- Located exact physical pinouts:
  - **Ground**: **J3 Pin 5 (GND)** directly on the expansion board (adjacent to signal pins).
  - **Primary Trigger Signal**: **J3 Pin 3 (Z+ / PB10 / TIM2_CH3)** or **J3 Pin 1 (A+ / PA15 / TIM2_CH1)**.
  - Optional Power: **J3 Pin 4 (+5V)**.
- Defined high-precision 1 MHz timer input capture math ($\Delta\text{ticks}$ period measurement providing up to $0.005\%$ accuracy at 3000 RPM) with native 32-bit modulo rollover handling.
- Formulated analytical stall vs. running vs. out-of-sync vibration detection model comparing optical shaft speed against commanded electrical commutation speed.
- Published strategy specification to `doc/features/07_optical_tachometer_and_stall_detection_strategy.md`.
