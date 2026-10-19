# Worklog: 24V (6S) Speed Ladder Analysis and Quad-Voltage Dataset Benchmark

- **Date**: 2026-10-18
- **Time**: 22:00:00
- **Agent**: Antigravity Assistant
- **User**: beattidp

---

## 1. Summary of Actions & Motivation

Executing the final stage of the 4-voltage test matrix on the live motor fixture with the bench power supply set to 24V (6S LiPo equivalent):
1. **Executed Automated 24V (6S) Speed Ladder (`test_runs/run_20261018_215731.csv`)**:
   - VBUS detected at 24.25 V.
   - Stepped through targets `[288, 864, 1728, 2880, 4320 RPM]`.
   - Total mechanical revolutions counted: **2,993 revs**.
   - Samples captured: 450 frames.
2. **Synchronism & High-Speed Performance**:
   - Flawless 1:1 synchronous lock maintained through 864 RPM, 1,728 RPM, 2,880 RPM, and up to **4,013 RPM**.
   - Rotor reached **468.2 Hz electrical frequency** and **66.9 mechanical revolutions per second**, verified by the optical photodetector.
   - Total revolutions across the 4-voltage testing suite reached **7,951 revs**.
3. **Comprehensive Quad-Voltage Dataset Synthesis**:
   - Synthesized comparative metrics across 12V (3S), 16V (4S), 20V (5S), and 24V (6S).
   - Documented the mathematical and empirical progression of voltage scaling, back-EMF ceilings, and open-loop V/F stability boundaries (~400 Hz electrical).

---

## 2. Power Stage State
- Sent `'r'` (clear fault) and `'z'` (zero revs).
- Board confirmed in `[READY / NORMAL]` state with 0 RPM stopped.
