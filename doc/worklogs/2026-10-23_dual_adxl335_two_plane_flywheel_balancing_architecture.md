# Worklog: Dual ADXL335 Integration & Two-Plane Flywheel Balancing Architecture

- **Date**: 2026-10-23
- **Time**: 02:29 MDT
- **Agent**: Antigravity Pair Programmer
- **User**: Dan Beattie

---

## 1. Summary of Actions
- **Researched ADXL335 Datasheet (`doc/datasheets/accelerometer/ADXL335BCPZ.pdf`)**:
  - Analyzed sensitivity ($300\text{ mV/g}$ at 3.0 V, $330\text{ mV/g}$ at 3.3 V), zero-g bias ($1.65\text{ V}$ at 3.3 V), $32\text{ k}\Omega$ internal filter resistance, and filter capacitor sizing ($C_X, C_Y, C_Z$).
  - Recommended $10\text{ nF}$ filter capacitors ($500\text{ Hz}$ bandwidth) for accurate unattenuated 1X vibration tracking up to 6000 RPM ($100\text{ Hz}$).
- **Engineered Spatial Kinematic Model & Two-Plane Dynamic Balancing Math**:
  - Derived coordinate decomposition for two 3-axis sensors located on diagonally opposite frame corners (top-front-left and lower-rear-right) to separate translational (static) and rocking moment (couple) unbalance modes.
  - Formulated the synchronous single-bin 1X Discrete Fourier Transform (quadrature demodulation) synchronized to the 1 PPR optical tachometer index pulse.
  - Derived the analytical solution for the $2 \times 2$ complex Influence Coefficient Matrix equation to calculate correction weights and angles across two axial planes.
  - Formulated discrete hole splitting algorithm for circumferential set-screw patterns.
- **Formulated Hardware Pin Allocation & Isolated ADC Architecture**:
  - Assigned the 6 analog channels across `ADC2` and `ADC4` (`PA4`, `PB11`, `PB12`, `PC4`, `PC5`, and `PC3`/`PB1`) to protect the critical 20 kHz injected motor current sampling on `ADC1`.
- **Created Documentation & Implementation Plan**:
  - Authored Feature 19 architecture specification in `doc/features/19_dual_adxl335_two_plane_flywheel_balancing_architecture.md`.
  - Created implementation plan artifact `implementation_plan.md` for user review and approval.
