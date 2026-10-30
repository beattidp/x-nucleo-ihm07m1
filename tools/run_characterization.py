#!/usr/bin/env python3
"""
Automated BLDC Motor Characterization Runner

Triggers and monitors the 5-phase automated characterization routine on the
STM32G474RE + X-NUCLEO-IHM07M1:
1. Shunt Offset Calibration
2. Phase Resistance (Rs) Measurement
3. Rotor Alignment Hold
4. Dynamic Spin (25 Hz & 50 Hz ramps under eddy current brake load)
5. Eddy Brake Damping & Load Compensation Calculation
6. Automatic persistence into Profile 3 via ENTER keystroke.
"""

import os
import re
import sys
import time

from capture_console import (
    get_target_session,
    send_keystroke,
    capture_snapshot,
    start_logging,
    stop_logging
)

def run_characterization():
    session_id = get_target_session()
    print(f"Connecting to screen session: {session_id}")

    temp_snap = "/tmp/char_pretest.txt"
    capture_snapshot(session_id, temp_snap)
    with open(temp_snap, "r", encoding="utf-8", errors="replace") as f:
        pre = f.read()
    if "FAULT" in pre:
        print("Clearing fault with 'r'...")
        send_keystroke(session_id, "r")
        time.sleep(0.5)
    if "RUNNING" in pre or "STARTING" in pre:
        print("Stopping motor with 'space'...")
        send_keystroke(session_id, "space")
        time.sleep(1.0)

    print("Initiating characterization routine ('c')...")
    send_keystroke(session_id, "c")
    time.sleep(0.5)

    start_time = time.time()
    last_pct = -1
    complete = False
    failed = False
    fail_reason = ""

    log_file = f"test_runs/char_run_{int(start_time)}.log"
    os.makedirs("test_runs", exist_ok=True)
    start_logging(session_id, log_file)

    while time.time() - start_time < 22.0:
        time.sleep(0.5)
        capture_snapshot(session_id, temp_snap)
        with open(temp_snap, "r", encoding="utf-8", errors="replace") as f:
            snap = f.read()

        m_prog = re.search(r"\[\s*([0-9]+)\s*%\s*\]", snap)
        m_step = re.search(r"Status\s*:\s*([^\n\r|]+)", snap)

        step_str = m_step.group(1).strip() if m_step else ""
        pct = int(m_prog.group(1)) if m_prog else 0

        if pct != last_pct or (int(time.time() - start_time) % 2 == 0):
            print(f"[{time.time() - start_time:4.1f}s] Progress: {pct:3d}% | Status: {step_str}")
            last_pct = pct

        snap_lower = snap.lower()
        if "characterization complete" in snap_lower or "press [enter] to save" in snap_lower or "[100%]" in snap_lower or pct >= 100:
            complete = True
            break

        if "aborted" in snap_lower or "failed" in snap_lower or "fault" in snap_lower:
            failed = True
            m_err = re.search(r"Status\s*:\s*ABORTED\s*\(([^)]+)\)", snap, re.IGNORECASE)
            fail_reason = m_err.group(1) if m_err else "Aborted / Faulted"
            break

    stop_logging(session_id)

    capture_snapshot(session_id, temp_snap)
    with open(temp_snap, "r", encoding="utf-8", errors="replace") as f:
        final_snap = f.read()

    print("\n" + "=" * 78)
    print("FINAL CHARACTERIZATION SNAPSHOT")
    print("=" * 78)
    print(final_snap)
    print("=" * 78)

    if complete:
        print("\nCharacterization succeeded! Saving and applying results with [ENTER]...")
        send_keystroke(session_id, "enter")
        time.sleep(1.0)
        
        capture_snapshot(session_id, temp_snap)
        with open(temp_snap, "r", encoding="utf-8", errors="replace") as f:
            post_snap = f.read()
        print("\nPost-save dashboard status:")
        for line in post_snap.splitlines():
            if any(k in line for k in ["Active Motor Profile", "Control Strategy", "Commanded Torque", "Active Modulated"]):
                print(f"  {line}")
        return True
    else:
        print(f"\nCharacterization failed or timed out: {fail_reason}")
        return False

if __name__ == "__main__":
    success = run_characterization()
    sys.exit(0 if success else 1)
