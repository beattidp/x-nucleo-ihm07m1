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

    if "FAULT" in content:
        print("Motor is in FAULT state. Sending RESET ('r')...")
        send_keystroke(session_id, "r")
        time.sleep(0.5)

    if "Drive State                : RUNNING" in content or "STARTING" in content:
        print("Motor is currently running or starting. Sending STOP...")
        send_keystroke(session_id, "space")
        time.sleep(1.0)


VOLTAGE_LADDERS = {
    12: [144, 432, 864, 1440, 2160, 3024],
    16: [192, 576, 1152, 1920, 2880, 4032],
    20: [240, 720, 1440, 2400, 3600, 5040],
    24: [288, 864, 1728, 2880, 4320, 6048]
}


def get_bus_voltage(session_id):
    """Reads live bus voltage from console snapshot."""
    snapshot_file = "/tmp/motor_vbus_snap.txt"
    capture_snapshot(session_id, snapshot_file)
    with open(snapshot_file, "r", encoding="utf-8", errors="replace") as f:
        snap = f.read()
    try:
        os.remove(snapshot_file)
    except OSError:
        pass

    m = re.search(r"Bus Voltage \(VBUS\)\s*:\s*([0-9.]+)\s*V", snap)
    if m:
        return float(m.group(1))
    return 12.0


def resolve_voltage_targets(vbus_v, bidirectional=True):
    """Selects speed target ladder based on measured bus voltage."""
    if vbus_v < 14.0:
        nom_v = 12
    elif vbus_v < 18.0:
        nom_v = 16
    elif vbus_v < 22.0:
        nom_v = 20
    else:
        nom_v = 24

    targets = VOLTAGE_LADDERS[nom_v]
    if bidirectional:
        # Accelerate up through all targets, then decelerate back down to initial target
        full_ladder = targets + targets[-2::-1]
    else:
        full_ladder = list(targets)
    return nom_v, full_ladder


def adjust_target_rpm(session_id, target_rpm):
    """Directly sets target RPM via console 's<RPM>\r' command."""
    send_keystroke(session_id, f"s{target_rpm}\r")
    time.sleep(0.1)
    print(f"Set target RPM directly to {target_rpm} RPM.")


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
        adjust_target_rpm(session_id, target_rpm)

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


def check_log_fault(log_file):
    if not os.path.exists(log_file):
        return False
    try:
        with open(log_file, "rb") as f:
            size = os.path.getsize(log_file)
            f.seek(max(0, size - 4096))
            tail = f.read().decode("utf-8", errors="replace")
            if "FAULT / OVERCURRENT TRIPPED" in tail or "Drive State                : FAULT" in tail:
                return True
    except OSError:
        pass
    return False


def wait_step_duration(session_id, log_file, duration_s):
    t_end = time.time() + duration_s
    while time.time() < t_end:
        time.sleep(min(1.0, max(0.1, t_end - time.time())))
        if check_log_fault(log_file):
            print("\n[!] DRIVER FAULT DETECTED IN STREAM! Aborting remaining steps early...")
            return False
    return True


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

    # Identify chronological continuous segments of target_rpm (distinguishes accel vs decel)
    segments = []
    curr_t = None
    curr_seg = []
    for s in samples:
        t = s.get("target_rpm")
        if t is None or s.get("motor_state") not in ("STARTING (ALIGN & RAMP)", "RUNNING"):
            continue
        if t != curr_t:
            if curr_seg:
                segments.append((curr_t, curr_seg))
            curr_t = t
            curr_seg = [s]
        else:
            curr_seg.append(s)
    if curr_seg:
        segments.append((curr_t, curr_seg))

    if len(segments) > 1:
        print("-" * 90)
        print(" MULTI-STAGE SPEED STEP RESPONSE & CURRENT CONSUMPTION BREAKDOWN:")
        print("-" * 90)
        print(f" {'Step':<5} | {'Phase':<7} | {'Target':<7} | {'Est End':<8} | {'Real Optical (Avg/End)':<22} | {'Phase Peak (Avg/Max)':<20} | {'Duty %':<7} | {'Status'}")
        print("-" * 90)
        max_seen = 0
        for idx, (t, t_samples) in enumerate(segments, start=1):
            if not t_samples:
                continue
            r_rpms = [s["optical_rpm"] if s.get("optical_rpm") is not None else s.get("real_rpm", 0) for s in t_samples]
            e_rpms = [s["est_rpm"] for s in t_samples if s.get("est_rpm") is not None]
            p_currs = [s["ipk_ma"] for s in t_samples if s.get("ipk_ma") is not None]
            duties = [s["active_duty_pct"] for s in t_samples if s.get("active_duty_pct") is not None]
            statuses = [s.get("optical_status", "") for s in t_samples]

            avg_r = sum(r_rpms) / len(r_rpms) if r_rpms else 0
            end_r = r_rpms[-1] if r_rpms else 0
            end_e = e_rpms[-1] if e_rpms else 0
            avg_p = sum(p_currs) / len(p_currs) if p_currs else 0
            max_p = max(p_currs) if p_currs else 0
            avg_d = sum(duties) / len(duties) if duties else 0
            has_slip = any("SLIP" in st or "VIB" in st for st in statuses)
            has_stall = any("STALL" in st or "LOCK" in st for st in statuses)
            cond = "SLIP/VIB" if has_slip else ("STALLED" if has_stall else "IN SYNC")

            if t > max_seen:
                phase = "ACCEL"
                max_seen = t
            elif t == max_seen:
                phase = "PEAK"
            else:
                phase = "DECEL"

            print(f" {idx:<5} | {phase:<7} | {t:<7} | {end_e:<8.0f} | {f'{avg_r:.0f} / {end_r}':<22} | {f'{avg_p:.0f} mA / {max_p} mA':<20} | {avg_d:<7.1f} | {cond}")

    print("=" * 80)
    return outcome


def main():
    parser = argparse.ArgumentParser(description="Automated Motor Test Run & Telemetry Verification")
    parser.add_argument("--session", "-s", help="Screen session ID (default: auto-detect)")
    parser.add_argument("--profile", "-p", type=int, choices=[1, 2, 3, 4], help="Motor profile (1-4)")
    parser.add_argument("--rpm", type=int, help="Target motor RPM")
    parser.add_argument("--speed-steps", help="Comma-separated speed steps (e.g. '300,700,1100' or 'auto')")
    parser.add_argument("--voltage-targets", action="store_true", help="Auto-detect bus voltage and use voltage-dependent speed targets with decel")
    parser.add_argument("--no-decel", action="store_true", help="Disable deceleration ladder in voltage targets")
    parser.add_argument("--step-duration", type=float, default=12.0, help="Duration per speed step in seconds (default: 12.0s)")
    parser.add_argument("--duty-adjust", type=int, default=0, help="Duty adjustment steps (+ for ], - for [)")
    parser.add_argument("--duration", "-d", type=float, default=6.0, help="Run duration in seconds (default: 6.0s)")
    parser.add_argument("--output-dir", default="test_runs", help="Directory to save logs and CSVs")
    args = parser.parse_args()

    session_id = get_target_session(args.session)
    os.makedirs(args.output_dir, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(args.output_dir, f"run_{ts}.log")
    csv_file = os.path.join(args.output_dir, f"run_{ts}.csv")

    speed_steps = None
    if args.voltage_targets or args.speed_steps == "auto":
        vbus = get_bus_voltage(session_id)
        nom_v, speed_steps = resolve_voltage_targets(vbus, bidirectional=not args.no_decel)
        print(f"Detected Bus Voltage: {vbus:.2f} V -> Selected {nom_v}V Speed Ladder: {speed_steps}")
        init_rpm = speed_steps[0]
    elif args.speed_steps:
        speed_steps = [int(x.strip()) for x in args.speed_steps.split(",")]
        init_rpm = speed_steps[0]
    else:
        init_rpm = args.rpm

    print(f"Starting automated motor test on screen session '{session_id}'...")
    ensure_stopped(session_id)

    configure_test(session_id, profile=args.profile, target_rpm=init_rpm, duty_adjust=args.duty_adjust)

    # Start stream logging
    start_logging(session_id, log_file, redraw=True)
    time.sleep(0.5)

    if speed_steps:
        print(f"Commanding MOTOR START (SPACE) -- Running Step 1: {speed_steps[0]} RPM for {args.step_duration:.1f}s...")
        send_keystroke(session_id, "space")
        try:
            if wait_step_duration(session_id, log_file, args.step_duration):
                for idx, target in enumerate(speed_steps[1:], start=2):
                    print(f"Stepping speed to {target} RPM (Step {idx}/{len(speed_steps)}) -- Holding for {args.step_duration:.1f}s...")
                    adjust_target_rpm(session_id, target)
                    if not wait_step_duration(session_id, log_file, args.step_duration):
                        break
        finally:
            print("Commanding MOTOR STOP (SPACE)...")
            send_keystroke(session_id, "space")
            time.sleep(1.0)
            stop_logging(session_id)
    else:
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
