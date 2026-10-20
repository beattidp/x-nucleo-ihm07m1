# Worklog: Upstream Commit Push and STM32 Hardware Hall Sensor Interface Configuration on TIM2

**Date**: 2026-10-20  
**Time**: 14:50 Mountain / 20:50 UTC  
**Agent**: Antigravity  
**User**: beattidp  

---

## 1. Summary of Actions
- **Upstream Push**:
  - Pushed 12 local commits (`305f478` through `fa11d46`) upstream to `origin/main` on `github.com:beattidp/x-nucleo-ihm07m1.git` following user authorization.
- **Hardware Hall Sensor Interface Configuration**:
  - Configured connector **J3** pins (`PA15`, `PB3`, `PB10`) as Alternate Function `GPIO_AF1_TIM2` for `TIM2` Channels 1, 2, and 3.
  - Enabled internal pullups on all 3 Hall inputs for compatibility with open-collector Hall sensor ICs.
  - Configured `TIM2` in STM32 hardware **Hall Sensor Interface Mode** (`HAL_TIMEx_HallSensor_Init`) utilizing the internal 3-input XOR gate (`TI1S`), filtered edge detector (`TI1F_ED`), and slave mode reset (`TIM_SLAVEMODE_RESET`).
  - Implemented `TIM2_IRQHandler()` and `Motor_HallTransitionISR()` to capture the $60^\circ$ electrical transition interval in `CCR1` (1.0 us tick) and compute instantaneous speed via $\text{RPM} = \frac{10^7}{P \cdot \Delta t_{\mu\text{s}}}$.
  - Updated `MotorTelemetry_t` and `Console_RenderDashboard` to report live Hall sector state, validity, and speed.
- **Target Flashing & Live Verification**:
  - Successfully built and flashed firmware via ST-Link.
  - Verified live console dashboard: cleanly reports `[SENSORLESS MOTOR / J4 DISCONNECTED]` when J3 is unpopulated.
  - Conducted spin test to 864 RPM to confirm that `TIM2` Hall mode does not impact or interfere with cascaded `TIM3`+`TIM4` optical tachometer operations on Morpho CN10 Pin 4 (`PC6`).
- **Mechanical & Test Bench Notes**:
  - User enhanced optical reflectivity by coating the top side of the motor with matte finish dark gray acrylic paint, leaving a single glossy white reflective section.
  - User noted work on an adjustable torque load fixture to evaluate closed-loop performance under loaded conditions.
  - Acknowledged that attaching a flywheel or adjustable load will require repositioning the optical tachometer hardware, and noted the upcoming user recess.

---

## 2. Code Changes

1. **`include/motor_control.h`**:
   - Added `hall_rpm`, `hall_transition_count`, and `hall_valid` fields to `MotorTelemetry_t`.
   - Declared `Motor_HallTransitionISR(uint8_t hall_state, uint32_t dt_us)` prototype.
2. **`src/ihm07m1.c`**:
   - Configured `PA15` (Hall 1), `PB3` (Hall 2), `PB10` (Hall 3) in `GPIO_MODE_AF_PP`, `GPIO_PULLUP`, `GPIO_AF1_TIM2`.
   - Configured `TIM2` via `HAL_TIMEx_HallSensor_Init()` and started CC1 interrupts via `HAL_TIMEx_HallSensor_Start_IT(&htim2)`.
   - Implemented `TIM2_IRQHandler()` to service Hall edge capture and dispatch to motor control.
3. **`src/motor_control.c`**:
   - Implemented `Motor_HallTransitionISR()` to track transition count and calculate Hall RPM with a 1.0 us timebase.
   - Added 250 ms timeout watchdog in `Motor_Update1ms()` to decay Hall speed to 0 RPM when rotation stops.
4. **`src/console.c`**:
   - Updated Row 13 dashboard telemetry to display live Hall sector and RPM when valid (`State %d (H321=%d%d%d) | %4d RPM [VALID]`), and `[SENSORLESS MOTOR / J4 DISCONNECTED]` when open.
5. **`doc/features/15_3phase_hardware_hall_sensor_interface_architecture.md`**:
   - Created detailed architecture specification, circuit schematic mapping, and commutation equations.
