#!/usr/bin/env python3
"""
Telemetry Stream & Event Sequence Reconstructor

Parses raw ANSI terminal log streams captured from the evaluation console.
Decodes ANSI cursor positioning escapes (\033[<row>;<col>H), reconstructs
differential telemetry updates correlated directly to the Sample #, and exports
event timelines, summary statistics, and time-series CSV datasets.
"""

import argparse
import csv
import os
import re
import sys


# Known fixed coordinate map from firmware (Row, Col)
COORD_MAP = {
    (5, 44): "sample_anchor",     # Sample: #0001234  [Heartbeat: /]
    (5, 11): "uptime",            # 00:01:23
    (9, 31): "vbus",              # 12.15 V [OK]
    (10, 31): "temp",             # 43.4 deg C
    (11, 31): "pot",              # 5.7 % [ACTIVE -> Target  454 RPM]
    (12, 31): "driver_status",    # [READY / NORMAL] or [FAULT / OVERCURRENT TRIPPED]
    (13, 31): "hall",             # State 1 [H1=1, H2=0, H3=0] or [SENSORLESS ...]
    (14, 31): "optical_tach",     # 1245 RPM (Revs:   482) [ROTATING - OK]
    (18, 31): "motor_state",      # STOPPED, STARTING, RUNNING, etc.
    (19, 31): "control_mode",     # STAGE 1: OPEN-LOOP V/F (SENSORLESS)
    (20, 31): "motor_profile",    # [2/4] Flywheel 5010-360KV (14 PP, Max 1.20 A)
    (21, 31): "speed",            # Target  150 | Est  150 | Real    0 RPM (CW )
    (22, 31): "torque_limit",     # 1.20 A (Software Regulated Ceiling)
    (23, 31): "duty",             # 10.0 % (Base  6.0 % | ARR = 4249)
    (24, 31): "currents",         # IA=   1 mA | IB=   4 mA | IC=   3 mA [Pk=   4 mA]
    (25, 31): "hw_trip_limit",    # 2.20 A (L6230 OCD Break)
    (30, 1): "cursor_park",
}

# Regex to strip ANSI styling escapes (e.g. bold, green, red, clear line)
ANSI_STYLE_RE = re.compile(r"\x1b\[[0-9;]*[mK]")
# Regex to match cursor positioning: \x1b[<row>;<col>H
ANSI_CUP_RE = re.compile(r"\x1b\[(\d+);(\d+)H")


class TelemetryReconstructor:
    def __init__(self):
        self.state = {
            "sample_num": None,
            "heartbeat": None,
            "uptime": "",
            "vbus_v": None,
            "vbus_status": "",
            "temp_c": None,
            "pot_pct": None,
            "pot_target_rpm": None,
            "driver_fault": False,
            "driver_status": "",
            "hall_state": "",
            "motor_state": "STOPPED",
            "control_mode": "",
            "profile_name": "",
            "target_rpm": None,
            "est_rpm": None,
            "real_rpm": None,
            "optical_rpm": 0,
            "optical_revs": 0,
            "optical_status": "",
            "direction": "CW",
            "torque_limit_amps": None,
            "active_duty_pct": None,
            "base_duty_pct": None,
            "ia_ma": None,
            "ib_ma": None,
            "ic_ma": None,
            "ipk_ma": None,
            "id_ma": None,
            "iq_ma": None,
            "hw_limit_amps": None,
        }
        self.samples = []
        self.events = []
        self._current_sample = None

    def _clean(self, text):
        return ANSI_STYLE_RE.sub("", text).strip()

    def process_field(self, row, col, raw_text):
        tag = COORD_MAP.get((row, col))
        clean_text = self._clean(raw_text)
        if not clean_text:
            return

        prev = dict(self.state)

        if tag == "sample_anchor" or "Sample: #" in clean_text:
            m = re.search(r"Sample:\s*#(\d+)\s*\[Heartbeat:\s*([^\]]+)\]", clean_text)
            if m:
                new_sample = int(m.group(1))
                hb = m.group(2).strip()
                # If we were already in a sample, commit it
                if self._current_sample is not None and self._current_sample != new_sample:
                    self.samples.append(dict(self.state))
                self.state["sample_num"] = new_sample
                self.state["heartbeat"] = hb
                self._current_sample = new_sample

        elif tag == "uptime":
            m = re.search(r"(\d{2}:\d{2}:\d{2})", clean_text)
            if m:
                self.state["uptime"] = m.group(1)

        elif tag == "vbus":
            m = re.search(r"([0-9.]+)\s*V\s*(.*)", clean_text)
            if m:
                self.state["vbus_v"] = float(m.group(1))
                self.state["vbus_status"] = m.group(2).strip()

        elif tag == "temp":
            m = re.search(r"([0-9.]+)\s*deg\s*C", clean_text)
            if m:
                new_temp = float(m.group(1))
                if prev["temp_c"] is not None and abs(new_temp - prev["temp_c"]) >= 0.5:
                    self._record_event("Temperature", f"{prev['temp_c']} °C -> {new_temp} °C")
                self.state["temp_c"] = new_temp

        elif tag == "pot":
            m = re.search(r"([0-9.]+)\s*%", clean_text)
            if m:
                self.state["pot_pct"] = float(m.group(1))
            m_rpm = re.search(r"Target\s+(\d+)\s+RPM", clean_text)
            if m_rpm:
                self.state["pot_target_rpm"] = int(m_rpm.group(1))

        elif tag == "driver_status":
            fault = "FAULT" in clean_text or "OVERCURRENT" in clean_text
            self.state["driver_fault"] = fault
            self.state["driver_status"] = clean_text
            if prev["driver_fault"] != fault:
                self._record_event("Driver Status", f"FAULT TRIPPED" if fault else "NORMAL / READY")

        elif tag == "hall":
            self.state["hall_state"] = clean_text

        elif tag == "optical_tach":
            m = re.search(r"(-?\d+)\s+RPM\s+\(Revs:\s*(\d+)\)(?:\s+(\[.+?\]))?", clean_text)
            if m:
                opt_rpm = int(m.group(1))
                opt_revs = int(m.group(2))
                opt_status = m.group(3).strip() if m.group(3) else ""
                if opt_status and prev.get("optical_status") and prev.get("optical_status") != opt_status:
                    self._record_event("Optical Tach Status", opt_status)
                self.state["optical_rpm"] = opt_rpm
                self.state["optical_revs"] = opt_revs
                if opt_status:
                    self.state["optical_status"] = opt_status

        elif tag == "motor_state":
            st = clean_text.strip()
            if prev["motor_state"] != st:
                self._record_event("Motor State Transition", f"{prev['motor_state']} -> {st}")
            self.state["motor_state"] = st

        elif tag == "control_mode":
            self.state["control_mode"] = clean_text

        elif tag == "motor_profile":
            self.state["profile_name"] = clean_text
            if prev["profile_name"] and prev["profile_name"] != clean_text:
                self._record_event("Motor Profile Selected", clean_text)

        elif tag == "speed":
            # New format: Target 150 | Est 150 | Real   0 RPM (CW )
            m = re.search(r"Target\s+(-?\d+)\s+\|\s+Est\s+(-?\d+)\s+\|\s+Real\s+(-?\d+)\s+RPM(?:\s+\(([A-Z]+)\))?", clean_text)
            if m:
                tgt = int(m.group(1))
                est = int(m.group(2))
                real = int(m.group(3))
                direction = m.group(4).strip() if m.group(4) else "CW"
                if prev["target_rpm"] is not None and prev["target_rpm"] != tgt:
                    self._record_event("Target Speed Demand", f"{prev['target_rpm']} RPM -> {tgt} RPM")
                self.state["target_rpm"] = tgt
                self.state["est_rpm"] = est
                self.state["real_rpm"] = real
                self.state["direction"] = direction
            else:
                # Legacy format: Target 150 RPM | Est 0 RPM (CW )
                m_leg = re.search(r"Target\s+(-?\d+)\s+RPM\s+\|\s+Est\s+(-?\d+)\s+RPM(?:\s+\(([A-Z]+)\))?", clean_text)
                if m_leg:
                    tgt = int(m_leg.group(1))
                    est = int(m_leg.group(2))
                    direction = m_leg.group(3).strip() if m_leg.group(3) else "CW"
                    if prev["target_rpm"] is not None and prev["target_rpm"] != tgt:
                        self._record_event("Target Speed Demand", f"{prev['target_rpm']} RPM -> {tgt} RPM")
                    self.state["target_rpm"] = tgt
                    self.state["est_rpm"] = est
                    self.state["direction"] = direction

        elif tag == "torque_limit":
            m = re.search(r"([0-9.]+)\s*A", clean_text)
            if m:
                val = float(m.group(1))
                if prev["torque_limit_amps"] is not None and abs(val - prev["torque_limit_amps"]) >= 0.05:
                    self._record_event("Torque Limit Adjusted", f"{prev['torque_limit_amps']:.2f} A -> {val:.2f} A")
                self.state["torque_limit_amps"] = val

        elif tag == "duty":
            m = re.search(r"([0-9.]+)\s*%\s*\(Base\s*([0-9.]+)", clean_text)
            if m:
                self.state["active_duty_pct"] = float(m.group(1))
                self.state["base_duty_pct"] = float(m.group(2))

        elif tag == "currents":
            m = re.search(r"IA=\s*(-?\d+)\s*(?:mA\s*\|\s*)?IB=\s*(-?\d+)\s*(?:mA\s*\|\s*)?IC=\s*(-?\d+)(?:\s*mA)?(?:\s*\[Pk=\s*(-?\d+)(?:\s*mA)?\])?(?:\s*\|\s*Id=\s*(-?\d+)\s*Iq=\s*(-?\d+))?", clean_text)
            if m:
                self.state["ia_ma"] = int(m.group(1))
                self.state["ib_ma"] = int(m.group(2))
                self.state["ic_ma"] = int(m.group(3))
                if m.group(4) is not None:
                    self.state["ipk_ma"] = int(m.group(4))
                if m.group(5) is not None:
                    self.state["id_ma"] = int(m.group(5))
                if m.group(6) is not None:
                    self.state["iq_ma"] = int(m.group(6))

        elif tag == "hw_trip_limit":
            m = re.search(r"([0-9.]+)\s*A(?:\s*\([^)]+\))?(?:\s*\|\s*Peak:\s*(-?\d+)\s*mA)?", clean_text)
            if m:
                self.state["hw_limit_amps"] = float(m.group(1))
                if m.group(2) is not None:
                    self.state["ipk_ma"] = int(m.group(2))

    def _record_event(self, event_type, details):
        sample = self.state["sample_num"] if self.state["sample_num"] is not None else 0
        uptime = self.state["uptime"] or "00:00:00"
        self.events.append({
            "sample": sample,
            "uptime": uptime,
            "type": event_type,
            "details": details,
        })

    def parse_stream(self, stream_data):
        """Tokenizes stream data by ANSI Cursor Positioning escapes \033[<row>;<col>H."""
        matches = list(ANSI_CUP_RE.finditer(stream_data))
        for i, match in enumerate(matches):
            row = int(match.group(1))
            col = int(match.group(2))
            next_start = match.end()
            next_end = matches[i + 1].start() if i + 1 < len(matches) else len(stream_data)
            token_text = stream_data[next_start:next_end]

            self.process_field(row, col, token_text)

        # Commit final sample if pending
        if self._current_sample is not None and (not self.samples or self.samples[-1]["sample_num"] != self._current_sample):
            self.samples.append(dict(self.state))


def export_csv(samples, csv_path):
    """Exports synchronized telemetry time-series to CSV."""
    if not samples:
        print("No samples to export.", file=sys.stderr)
        return

    fieldnames = [
        "sample_num", "uptime", "heartbeat", "vbus_v", "vbus_status",
        "temp_c", "pot_pct", "pot_target_rpm", "driver_fault",
        "motor_state", "control_mode", "profile_name",
        "target_rpm", "est_rpm", "real_rpm", "optical_rpm", "optical_revs", "optical_status",
        "direction", "torque_limit_amps", "active_duty_pct", "base_duty_pct",
        "ia_ma", "ib_ma", "ic_ma", "ipk_ma", "id_ma", "iq_ma", "hw_limit_amps"
    ]

    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for s in samples:
            writer.writerow({k: s.get(k, "") for k in fieldnames})
    print(f"Exported {len(samples)} synchronized samples to: {csv_path}")


def print_summary(samples, events):
    """Prints high-level summary of parsed stream."""
    print("=" * 80)
    print("           BLDC TELEMETRY STREAM & EVENT ANALYSIS SUMMARY")
    print("=" * 80)
    if not samples:
        print("No valid telemetry sample anchors found in stream.")
        return

    first = samples[0]
    last = samples[-1]

    s_start = first.get("sample_num", 0)
    s_end = last.get("sample_num", 0)
    total_samples = len(samples)
    duration_samples = (s_end - s_start) if (s_end and s_start) else 0

    vbus_vals = [s["vbus_v"] for s in samples if s.get("vbus_v") is not None]
    temp_vals = [s["temp_c"] for s in samples if s.get("temp_c") is not None]
    peak_currs = [s["ipk_ma"] for s in samples if s.get("ipk_ma") is not None]
    est_rpms = [s["est_rpm"] for s in samples if s.get("est_rpm") is not None]
    opt_rpms = [s["optical_rpm"] for s in samples if s.get("optical_rpm") is not None]
    opt_revs = [s["optical_revs"] for s in samples if s.get("optical_revs") is not None]

    print(f" Total Synchronized Frames  : {total_samples}")
    print(f" Sample Range               : #{s_start} -> #{s_end} (Span: {duration_samples} samples)")
    print(f" Uptime Window              : {first.get('uptime', 'N/A')} -> {last.get('uptime', 'N/A')}")
    print(f" Active Motor Profile       : {last.get('profile_name', 'N/A')}")
    print(f" Final Motor State          : {last.get('motor_state', 'N/A')}")
    if vbus_vals:
        print(f" Bus Voltage Range          : Min {min(vbus_vals):.2f} V | Max {max(vbus_vals):.2f} V | Avg {sum(vbus_vals)/len(vbus_vals):.2f} V")
    if temp_vals:
        print(f" Board Temperature Range    : Min {min(temp_vals):.1f} °C | Max {max(temp_vals):.1f} °C")
    if peak_currs:
        print(f" Peak Phase Current Range   : Min {min(peak_currs)} mA | Max {max(peak_currs)} mA | Avg {sum(peak_currs)/len(peak_currs):.1f} mA")
    if est_rpms:
        print(f" Commanded/Est Speed Range  : Min {min(est_rpms)} RPM | Max {max(est_rpms)} RPM")
    if opt_rpms:
        max_opt = max(opt_rpms)
        final_opt = opt_rpms[-1] if opt_rpms else 0
        total_revs = (max(opt_revs) - min(opt_revs)) if opt_revs else 0
        print(f" Optical Real Speed Range   : Max {max_opt} RPM | Final {final_opt} RPM | Revs Delta: {total_revs}")
        if last.get("optical_status"):
            print(f" Optical Tachometer Status  : {last.get('optical_status')}")

    print("-" * 80)
    print(f" DETECTED EVENTS TIMELINE ({len(events)} events):")
    print("-" * 80)
    if not events:
        print("  (No discrete state or parameter change events occurred during window)")
    else:
        for ev in events:
            print(f"  [Sample #{ev['sample']:07d} | {ev['uptime']}] {ev['type']}: {ev['details']}")
    print("=" * 80)


def main():
    parser = argparse.ArgumentParser(description="Reconstruct telemetry and sequence of events from console logs")
    parser.add_argument("logfile", nargs="?", help="Path to console ANSI log file (or stdin if omitted)")
    parser.add_argument("--csv", metavar="FILE", help="Export time-series telemetry records to CSV")
    parser.add_argument("--events", action="store_true", help="Print detailed event sequence timeline")
    parser.add_argument("--summary", action="store_true", default=True, help="Print summary statistics")
    args = parser.parse_args()

    if args.logfile:
        if not os.path.isfile(args.logfile):
            print(f"Error: Log file '{args.logfile}' does not exist.", file=sys.stderr)
            sys.exit(1)
        with open(args.logfile, "r", encoding="utf-8", errors="replace") as f:
            data = f.read()
    else:
        data = sys.stdin.read()

    recon = TelemetryReconstructor()
    recon.parse_stream(data)

    if args.summary:
        print_summary(recon.samples, recon.events)

    if args.csv:
        export_csv(recon.samples, args.csv)


if __name__ == "__main__":
    main()
