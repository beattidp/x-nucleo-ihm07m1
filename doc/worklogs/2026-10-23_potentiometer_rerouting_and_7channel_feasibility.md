# Worklog: Potentiometer Re-Routing & 7-Channel Analog Architecture Feasibility

- **Date**: 2026-10-23
- **Time**: 19:44 MDT
- **Agent**: Antigravity Pair Programmer
- **User**: Dan Beattie

---

## 1. Summary of Actions
- **Traced Potentiometer Hardware Implementation on X-NUCLEO-IHM07M1**:
  - Located trimmer potentiometer $R_{42}$ (100 k$\Omega$) on schematic sheets 2 and 3.
  - Verified wiper connects to net `SPEED`, which routes through $0\,\Omega$ SMD resistor $R_{78}$ directly to MCU pin `PB1` (Morpho `CN10 Pin 24`).
- **Evaluated Potentiometer Re-Routing Options**:
  - Analyzed the physical and electrical options for hosting both 6 accelerometer channels and a potentiometer (7 analog channels total):
    1. **Zero-Bodge 7-Channel Architecture**: Leave trimmer $R_{42}$ on `PB1`, route 6 accelerometer channels to `PA4`, `PC4`, `PC5`, `PB11`, `PB12`, and `PC3` (with $R_{59}$ removed). Modifies only one resistor, zero jumper wires.
    2. **Onboard Trimmer Re-Routing**: Desolder $R_{78}$ to free `PB1` for an accelerometer, and fly a jumper wire from the wiper of $R_{42}$ to `PC3` or `PB0`.
    3. **External Potentiometer**: Wire a remote panel potentiometer to `PB0` (Arduino `A3` / Morpho `CN7:34`, with $R_{60}$ removed) or `PC3` (`CN7:37`, with $R_{59}$ removed).
    4. **Digital Rotary Encoder Alternative**: Connect an incremental quadrature encoder to spare timer pins `PB8`/`PB9` or `PB6`/`PB7` (`TIM4`), freeing all analog channels for vibration analysis and eliminating analog noise.
- **Updated Documentation**:
  - Added Section 6 to `doc/features/18_analog_inputs_io_pin_allocation_feasibility.md`.
