# Feature Specification: Closed-Loop Current Limiter & Hardware Diagnostics

- **Status**: Implemented & Verified
- **Date**: 2026-10-17
- **Source Modules**: [`src/motor_control.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_control.c), [`src/ihm07m1.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/ihm07m1.c), [`src/console.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/console.c)

---

## 1. Overview
The L6230 DMOS bridge has a continuous current rating of $1.4\text{ A RMS}$ per phase and an absolute maximum peak limit of $2.8\text{ A}$. Without proactive software regulation, sudden torque disturbances or high open-loop speed commands cause current to surge into the hardware trip limit ($2.2\text{ A}$ comparator reference), triggering `TIM1_BKIN` and halting the drive.

---

## 2. Technical Architecture

### 2.1 Fast Proportional Foldback Current Limiter
The 1 kHz control loop continuously monitors peak 3-phase current ($I_{\text{pk}} = \max(|I_A|, |I_B|, |I_C|)$). 
If $I_{\text{pk}}$ exceeds the active motor profile's continuous rating $I_{\text{limit}}$:
$$\Delta I = I_{\text{pk}} - I_{\text{limit}}$$
$$\text{Duty}_{\text{effective}} = \text{Duty}_{\text{commanded}} - (K_p \times \Delta I)$$
- When load torque increases, duty cycle automatically folds back smoothly rather than tripping the hardware breaker.
- When load torque subsides, duty cycle recovers seamlessly up to the user-commanded setpoint.

### 2.2 L6230 Hardware Overcurrent Trip Generation (PB4)
The L6230 comparator threshold is set via a high-frequency filtered PWM voltage generated on `PB4` (`TIM16_CH1`):
$$V_{\text{ref}} = I_{\text{trip}} \times R_{\text{shunt}} \times A_{\text{opamp}}$$
On the X-NUCLEO-IHM07M1 ($R_{\text{shunt}} = 0.33\ \Omega$, non-inverting gain $= 1.53$):
$$V_{\text{ref}} = I_{\text{trip}} \times 0.505\text{ V/A}$$
For $I_{\text{trip}} = 2.2\text{ A}$, $V_{\text{ref}} = 1.11\text{ V}$.

### 2.3 Diagnostic Filtering & Debounce (PA6 BKIN)
The L6230 `DIAG` pin is an open-drain combined overcurrent and thermal shutdown indicator tied to `PA6` (`TIM1_BKIN`):
- `PA6` is configured in Open-Drain mode with internal Pull-Up (`GPIO_MODE_AF_OD` with `GPIO_PULLUP`).
- TIM1 Break digital filter is set to $15$ ($1.5\ \mu\text{s}$ digital debounce filter) to prevent high-frequency switching noise from causing spurious motor trips.
