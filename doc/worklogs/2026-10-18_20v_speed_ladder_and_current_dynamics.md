# Worklog: 20V (5S) Speed Ladder Analysis and Open-Loop Stability Limits

- **Date**: 2026-10-18
- **Time**: 21:55:00
- **Agent**: Antigravity Assistant
- **User**: beattidp

---

## 1. Summary of Actions & Motivation

Following user adjustment of the bench power supply to 20V (5S LiPo equivalent):
1. **Executed Automated 20V (5S) Speed Ladder (`test_runs/run_20261018_215310.csv`)**:
   - VBUS detected at 20.20 V.
   - Stepped through targets `[240, 720, 1440, 2400, 3600 RPM]`.
   - Total mechanical revolutions counted: **2,065 revs**.
   - Samples captured: 370 frames.
2. **Synchronism & High-Speed Performance**:
   - Flawless 1:1 synchronous lock maintained through 1,440 RPM and 2,400 RPM (steady-state current ~20 mA).
   - Ramped up through 3,184 RPM (3,175 optical RPM).
   - Encountered hardware break trip at 3,184 RPM ($f_{\text{elec}} = 371.5\text{ Hz}$) due to phase angle hunting producing a 1.822 A instantaneous current surge.
3. **Tri-Voltage Comparative Synthesis**:
   - Analyzed the physical transition from pure DC bus voltage depletion (12V) to sinusoidal modulation limit (16V) to open-loop rotor angle instability / phase surge (20V).

---

## 2. Next Steps

1. Clear fault and reset telemetry on board.
2. Await user adjustment of bench power supply to **24V (6S)**.
3. Execute final 24V (6S) ladder test to complete the 4-voltage dataset.
