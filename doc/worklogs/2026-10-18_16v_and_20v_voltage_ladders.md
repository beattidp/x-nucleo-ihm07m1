# Worklog: 16V (4S) Speed Ladder Analysis and 20V (5S) Target Suite Integration

- **Date**: 2026-10-18
- **Time**: 21:38:00
- **Agent**: Antigravity Assistant
- **User**: beattidp

---

## 1. Summary of Actions & Motivation

In response to user feedback correlating the voltage ladders with 3S, 4S, 5S, and 6S battery configurations:
1. **Added 20V (5S) Speed Ladder Configuration**:
   - Integrated 20V (5S) into `doc/notes.txt` and `tools/run_motor_test.py`:
     $$\text{Targets: } [240, 720, 1440, 2400, 3600, 5040\text{ RPM}]$$
   - Mapped 4 battery pack voltage bands:
     - **3S (12V)**: `[144, 432, 864, 1440, 2160, 3024 RPM]`
     - **4S (16V)**: `[192, 576, 1152, 1920, 2880, 4032 RPM]`
     - **5S (20V)**: `[240, 720, 1440, 2400, 3600, 5040 RPM]`
     - **6S (24V)**: `[288, 864, 1728, 2880, 4320, 6048 RPM]`
2. **Executed Empirical 16V (4S) Speed Ladder Test (`test_runs/run_20261018_213627.csv`)**:
   - With power supply adjusted to 16.16 V VBUS, executed the 16V ladder with 15% base duty.
   - Total mechanical revolutions: **1,609 revs**.
   - Successfully unlocked **2,880 RPM** in solid 1:1 synchronism (which had been unreachable at 12V).
   - Ramped cleanly to **3,060 RPM** (3,056 optical RPM) before encountering the 16V sinusoidal modulation ceiling ($V_{\text{BEMF}} = 8.54\text{ V} > \frac{V_{\text{BUS}}}{2} = 8.08\text{ V}$).
3. **Firmware & Hardware Fault Clearing**:
   - Sent `'r'` (clear fault) and `'z'` (zero revs) to restore the power stage to `[READY / NORMAL]`.

---

## 2. Key Observations & Comparison: 12V vs 16V

- At 12V, the motor topped out at 2,695 RPM (80.1% duty).
- At 16V, the motor ran stably at **2,880 RPM** at only 57.5% duty with nominal phase current of only **36 mA**.
- Ramping beyond 3,060 RPM on 16V requires higher bus voltage (20V for up to 3,600–5,040 RPM, and 24V for up to 6,048 RPM).
