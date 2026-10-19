#!/usr/bin/env python3
"""
Automated BLDC Motor Test Runner & Diagnostic Analyzer

Orchestrates automated motor test runs via the GNU screen console session:
1. Configures motor profile, target speed (RPM), and duty adjustments.
2. Zeros optical revolution counter.
3. Initiates live stream logging.
4. Starts the motor ('space') and monitors telemetry for a configured duration.
5. Stops the motor and finalizes logging.
6. Reconstructs synchronized telemetry and outputs an automated diagnostic report:
   - Verifies whether the rotor actually rotated (optical pulse count > 0)
   - Compares commanded/estimated RPM against measured optical RPM
   - Identifies whether the motor spun and maintained sync, or stalled/vibrated
   - Calculates time-to-stall and phase current profile during stall
"""

import argparse
import datetime
import os
import re
import sys
import time

from capture_console import get_target_session, send_keystroke, start_logging, stop_logging, capture_snapshot
from reconstruct_telemetry import TelemetryReconstructor, export_csv


def ensure_stopped(session_id):
    """Ensures motor is in STOPPED state before configuring a test."""
    snapshot_file = "/tmp/motor_pretest_snap.txt"
    capture_snapshot(session_id, snapshot_file)
    with open(snapshot_file, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()
    try:
        os.remove(snapshot_file)
    except OSError:
        pass

    if "Drive State                : RUNNING" in content or "STARTING" in content:
        print("Motor is currently running or starting. Sending STOP...")
        send_keystroke(session_id, "space")
        time.sleep(1.0)


def configure_test(session_id, profile=None, target_rpm=None, duty_adjust=0):
    """Applies profile, speed, and duty configurations while stopped."""
    if profile is not None:
        if profile not in [1, 2, 3, 4]:
            raise ValueError(f"Invalid profile {profile}. Must be 1, 2, 3, or 4.")
        print(f"Selecting Motor Profile {profile}...")
        send_keystroke(session_id, str(profile))
        time.sleep(0.3)

    if target_rpm is not None:
        print(f"Setting target RPM to {target_rpm}...")
        # First press '>' once to disable potentiometer override and take manual control
        send_keystroke(session_id, ">")
        time.sleep(0.1)
        # Check current speed via snapshot
        snapshot_file = "/tmp/motor_speed_snap.txt"
        capture_snapshot(session_id, snapshot_file)
        with open(snapshot_file, "r", encoding="utf-8", errors="replace") as f:
            snap = f.read()
        try:
            os.remove(snapshot_file)
        except OSError:
            pass

        m = re.search(r"Target\s+(\d+)\s+\|\s+Est", snap)
        if m:
            curr_rpm = int(m.group(1))
            delta = target_rpm - curr_rpm
            steps = int(round(delta / 25.0))
            if steps > 0:
                for _ in range(steps):
                    send_keystroke(session_id, ">")
                    time.sleep(0.04)
            elif steps < 0:
                for _ in range(abs(steps)):
                    send_keystroke(session_id, "<")
                    time.sleep(0.04)
            print(f"Adjusted target RPM from {curr_rpm} to ~{target_rpm} ({steps} steps of 25 RPM).")

    if duty_adjust != 0:
        key = "]" if duty_adjust > 0 else "["
        print(f"Applying {abs(duty_adjust)} duty steps ({key})...")
        for _ in range(abs(duty_adjust)):
            send_keystroke(session_id, key)
            time.sleep(0.05)

    # Reset optical revolution counter
    print("Zeroing optical revolution counter...")
    send_keystroke(session_id, "z")
    time.sleep(0.1)


def analyze_test_run(samples, events):
    """Analyzes captured time-series telemetry for rotation, sync lock, or stall/vibrate."""
    if not samples:
        print("ERROR: No telemetry samples captured during test run.", file=sys.stderr)
        return

    first = samples[0]
    last = samples[-1]

    profile = last.get("profile_name", "Unknown")
    target_rpm = max([s["target_rpm"] for s in samples if s.get("target_rpm") is not None] or [0])
    base_duty = last.get("base_duty_pct", 0)
    active_duty = max([s["active_duty_pct"] for s in samples if s.get("active_duty_pct") is not None] or [0])

    active_samples = [s for s in samples if s.get("motor_state") in ("STARTING (ALIGN & RAMP)", "RUNNING")]
    running_samples = [s for s in samples if s.get("motor_state") == "RUNNING"]

    opt_rpms = [s["optical_rpm"] if s.get("optical_rpm") is not None else s.get("real_rpm", 0) for s in active_samples]
    opt_revs = [s["optical_revs"] for s in samples if s.get("optical_revs") is not None]
    peak_currs = [s["ipk_ma"] for s in active_samples if s.get("ipk_ma") is not None]

    max_opt_rpm = max(opt_rpms) if opt_rpms else 0
    total_revs = (max(opt_revs) - min(opt_revs)) if opt_revs else 0
    max_peak_ma = max(peak_currs) if peak_currs else 0
    avg_peak_ma = sum(peak_currs) / len(peak_currs) if peak_currs else 0

    # Stall & slip event detection during active driving window
    stall_events = [e for e in events if "STALLED" in e.get("details", "") or "LOCKED" in e.get("details", "")]
    slip_events = [e for e in events if "SLIPPING" in e.get("details", "") or "VIBRATING" in e.get("details", "")]

    # Check steady-state speed in RUNNING state
    running_rpms = [s["optical_rpm"] if s.get("optical_rpm") is not None else s.get("real_rpm", 0) for s in running_samples]
    avg_running_rpm = (sum(running_rpms) / len(running_rpms)) if running_rpms else 0
    final_running_rpm = running_rpms[-1] if running_rpms else 0

    print("\n" + "=" * 80)
    print("                    AUTOMATED MOTOR RUN DIAGNOSTIC REPORT")
    print("=" * 80)
    print(f" Profile Under Test          : {profile}")
    print(f" Commanded Target Speed      : {target_rpm} RPM")
    print(f" Base Duty / Peak Duty       : {base_duty:.1f} % / {active_duty:.1f} %")
    print(f" Total Synchronized Samples  : {len(samples)} frames ({len(active_samples)} active drive frames)")
    print(f" Current Draw (Peak Phase)   : Max {max_peak_ma} mA | Avg {avg_peak_ma:.1f} mA")
    print("-" * 80)
    print(" OPTICAL TACHOMETER VERIFICATION:")
    print(f"  * Total Revolutions Counted: {total_revs} rev(s)")
    print(f"  * Maximum Optical Speed    : {max_opt_rpm} RPM (Commanded: {target_rpm} RPM)")
    print(f"  * Steady-State Running RPM : Avg {avg_running_rpm:.1f} RPM | Final {final_running_rpm} RPM")

    # Outcome evaluation
    print("-" * 80)
    print(" ROTATION & SYNCHRONISM EVALUATION:")
    if total_revs == 0 and max_opt_rpm == 0:
        outcome = "FAILED_NO_ROTATION"
        print("  [X] ROTOR FAILED TO TURN: Zero optical pulses detected.")
        print("      The motor was unable to overcome static friction or stiction at start.")
    elif stall_events or (final_running_rpm == 0 and len(running_samples) > 5):
        outcome = "RAMPED_THEN_STALLED"
        print("  [!] CONFIRMED: Motor started/ramped up, then stalled and slipped into vibration!")
        print(f"      - Revolutions before stall : {total_revs}")
        print(f"      - Peak speed reached       : {max_opt_rpm} RPM")
        if stall_events:
            print(f"      - First stall detected at  : Sample #{stall_events[0]['sample']} (Uptime {stall_events[0]['uptime']})")
        if slip_events:
            print(f"      - First slip detected at   : Sample #{slip_events[0]['sample']} (Uptime {slip_events[0]['uptime']})")
    elif total_revs >= 1 and (final_running_rpm > 10 or avg_running_rpm > 10):
        outcome = "SPINNING_CONTINUOUS_SYNC"
        print("  [PASS] MOTOR ROTATING IN SYNCHRONISM: Motor ramped up and kept spinning!")
        print(f"      - Steady-state optical speed : {final_running_rpm} RPM (Avg: {avg_running_rpm:.1f} RPM)")
        print(f"      - Total rotations            : {total_revs} rev(s)")
    else:
        outcome = "PARTIAL_OR_UNSTABLE"
        print(f"  [?] PARTIAL ROTATION / SLIP: Detected {total_revs} revs, max {max_opt_rpm} RPM.")

    print("=" * 80)
    return outcome


def main():
    parser = argparse.ArgumentParser(description="Automated Motor Test Run & Telemetry Verification")
    parser.add_argument("--session", "-s", help="Screen session ID (default: auto-detect)")
    parser.add_argument("--profile", "-p", type=int, choices=[1, 2, 3, 4], help="Motor profile (1-4)")
    parser.add_argument("--rpm", type=int, help="Target motor RPM")
    parser.add_argument("--duty-adjust", type=int, default=0, help="Duty adjustment steps (+ for ], - for [)")
    parser.add_argument("--duration", "-d", type=float, default=6.0, help="Run duration in seconds (default: 6.0s)")
    parser.add_argument("--output-dir", default="test_runs", help="Directory to save logs and CSVs")
    args = parser.parse_args()

    session_id = get_target_session(args.session)
    os.makedirs(args.output_dir, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(args.output_dir, f"run_{ts}.log")
    csv_file = os.path.join(args.output_dir, f"run_{ts}.csv")

    print(f"Starting automated motor test on screen session '{session_id}'...")
    ensure_stopped(session_id)

    configure_test(session_id, profile=args.profile, target_rpm=args.rpm, duty_adjust=args.duty_adjust)

    # Start stream logging
    start_logging(session_id, log_file, redraw=True)
    time.sleep(0.5)

    print(f"Commanding MOTOR START (SPACE) -- Running for {args.duration:.1f} seconds...")
    send_keystroke(session_id, "space")

    try:
        time.sleep(args.duration)
    finally:
        print("Commanding MOTOR STOP (SPACE)...")
        send_keystroke(session_id, "space")
        time.sleep(1.0)
        stop_logging(session_id)

    print(f"Raw log saved to {log_file} ({os.path.getsize(log_file)} bytes).")

    # Reconstruct telemetry
    recon = TelemetryReconstructor()
    with open(log_file, "r", encoding="utf-8", errors="replace") as f:
        recon.parse_stream(f.read())

    export_csv(recon.samples, csv_file)
    analyze_test_run(recon.samples, recon.events)


if __name__ == "__main__":
    main()
