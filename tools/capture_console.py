#!/usr/bin/env python3
"""
Console Screen Session Manager & Capture Utility

Manages GNU screen sessions connected to the ST-LINK V3E Virtual COM Port (/dev/ttyACM0).
Supports:
1. Instant rendered buffer snapshot capture (hardcopy).
2. Live raw ANSI escape sequence stream logging to file.
3. Automated timed captures with optional initial screen redraw.
4. Screen session status inspection.
"""

import argparse
import os
import re
import subprocess
import sys
import time


def find_screen_sessions():
    """Finds active GNU screen sessions matching Nucleo / ttyACM0."""
    try:
        output = subprocess.check_output(["screen", "-ls"], stderr=subprocess.STDOUT, text=True)
    except subprocess.CalledProcessError as e:
        output = e.output

    sessions = []
    # Lines like: \t44291.pts-1.tantalum\t(10/17/2026 10:03:13 AM)\t(Attached)
    for line in output.splitlines():
        match = re.search(r"\s*(\d+\.[^\s]+)\s+\((.*?)\)\s+\((Attached|Detached)\)", line)
        if match:
            sessions.append({
                "id": match.group(1),
                "date": match.group(2),
                "state": match.group(3)
            })
    return sessions


def get_target_session(session_arg=None):
    """Resolves target screen session ID."""
    sessions = find_screen_sessions()
    if not sessions:
        print("Error: No active GNU screen sessions found.", file=sys.stderr)
        sys.exit(1)

    if session_arg:
        for s in sessions:
            if session_arg in s["id"]:
                return s["id"]
        print(f"Error: Specified session '{session_arg}' not found.", file=sys.stderr)
        sys.exit(1)

    # Return the first attached or available session
    for s in sessions:
        if s["state"] == "Attached":
            return s["id"]
    return sessions[0]["id"]


def capture_snapshot(session_id, output_path=None, include_scrollback=False):
    """Takes an instantaneous hardcopy snapshot of the screen buffer."""
    temp_file = output_path or "/tmp/console_snapshot.txt"
    abs_temp = os.path.abspath(temp_file)

    cmd = ["screen", "-S", session_id, "-p", "0", "-X", "hardcopy"]
    if include_scrollback:
        cmd.append("-h")
    cmd.append(abs_temp)

    ret = subprocess.run(cmd)
    if ret.returncode != 0:
        print(f"Error executing hardcopy command on session {session_id}", file=sys.stderr)
        sys.exit(1)

    time.sleep(0.1)
    if not os.path.isfile(abs_temp):
        print(f"Error: Hardcopy file was not generated at {abs_temp}", file=sys.stderr)
        sys.exit(1)

    with open(abs_temp, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    if output_path:
        print(f"Snapshot successfully saved to: {abs_temp}")
    else:
        print(content)
        try:
            os.remove(abs_temp)
        except OSError:
            pass


def trigger_redraw(session_id):
    """Sends 'l' to the screen session to trigger a complete dashboard redraw."""
    subprocess.run(["screen", "-S", session_id, "-p", "0", "-X", "stuff", "l"], check=True)


def start_logging(session_id, log_file, redraw=False):
    """Enables live stream logging to the specified file."""
    abs_path = os.path.abspath(log_file)
    os.makedirs(os.path.dirname(abs_path), exist_ok=True)

    # Ensure log is stopped first to flush any existing state
    subprocess.run(["screen", "-S", session_id, "-p", "0", "-X", "log", "off"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # Set logfile
    subprocess.run(["screen", "-S", session_id, "-p", "0", "-X", "logfile", abs_path], check=True)
    # Enable logging
    subprocess.run(["screen", "-S", session_id, "-p", "0", "-X", "log", "on"], check=True)
    print(f"Live stream logging STARTED on session '{session_id}' -> {abs_path}")

    if redraw:
        time.sleep(0.05)
        trigger_redraw(session_id)
        print("Triggered initial dashboard full redraw.")


def stop_logging(session_id):
    """Disables live stream logging."""
    subprocess.run(["screen", "-S", session_id, "-p", "0", "-X", "log", "off"], check=True)
    print(f"Live stream logging STOPPED on session '{session_id}'.")


def main():
    parser = argparse.ArgumentParser(description="GNU Screen Console Manager & Telemetry Stream Capture")
    parser.add_argument("--session", "-s", help="Screen session ID (default: auto-detect)")
    parser.add_argument("--status", action="store_true", help="List active screen sessions")
    parser.add_argument("--snapshot", nargs="?", const="print", help="Take screen buffer snapshot (save to file, or stdout if no file)")
    parser.add_argument("--scrollback", "-b", action="store_true", help="Include full scrollback history in snapshot")
    parser.add_argument("--redraw", action="store_true", help="Trigger a full screen redraw ('l')")
    parser.add_argument("--start-log", metavar="FILE", help="Start raw ANSI stream logging to FILE")
    parser.add_argument("--stop-log", action="store_true", help="Stop stream logging")
    parser.add_argument("--capture-duration", type=float, metavar="SECONDS", help="Capture stream logging for N seconds, then stop")
    parser.add_argument("--output", "-o", default="console_capture.log", help="Output file for --capture-duration (default: console_capture.log)")
    args = parser.parse_args()

    if args.status:
        sessions = find_screen_sessions()
        if not sessions:
            print("No active GNU screen sessions found.")
            return
        print(f"Found {len(sessions)} active screen session(s):")
        for s in sessions:
            print(f"  * [{s['id']}] ({s['state']}) started at {s['date']}")
        return

    session_id = get_target_session(args.session)

    if args.redraw and not args.capture_duration and not args.start_log:
        trigger_redraw(session_id)
        print(f"Full screen redraw triggered on session '{session_id}'.")
        return

    if args.snapshot:
        out = None if args.snapshot == "print" else args.snapshot
        capture_snapshot(session_id, out, args.scrollback)
        return

    if args.start_log:
        start_logging(session_id, args.start_log, args.redraw)
        return

    if args.stop_log:
        stop_logging(session_id)
        return

    if args.capture_duration:
        start_logging(session_id, args.output, args.redraw)
        print(f"Capturing stream for {args.capture_duration:.1f} seconds...")
        try:
            time.sleep(args.capture_duration)
        except KeyboardInterrupt:
            print("\nCapture interrupted by user.")
        finally:
            stop_logging(session_id)
            if os.path.isfile(args.output):
                print(f"Captured {os.path.getsize(args.output)} bytes in {args.output}")
        return

    parser.print_help()


if __name__ == "__main__":
    main()
