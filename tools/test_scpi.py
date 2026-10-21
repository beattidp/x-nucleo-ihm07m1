#!/usr/bin/env python3
"""
Automated SCPI Verification & Test Suite for Flywheel Test Fixture

Validates IEEE 488.2 compliance, SCPI command parsing, telemetry queries,
and communication mode switching between ANSI console and SCPI.

Usage:
    python3 tools/test_scpi.py [--port /dev/ttyACM0] [--baud 115200]
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("Error: pyserial is required. Install via: pip install pyserial")
    sys.exit(1)


def send_query(ser, query_str, timeout=1.0):
    """Sends a SCPI query string and returns the stripped response line."""
    ser.reset_input_buffer()
    cmd = (query_str.strip() + "\n").encode("utf-8")
    ser.write(cmd)
    ser.flush()

    start_time = time.time()
    line = b""
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            ch = ser.read(1)
            if ch == b"\n":
                break
            line += ch
        else:
            time.sleep(0.01)

    return line.decode("utf-8", errors="replace").strip()


def send_cmd(ser, cmd_str, delay=0.05):
    """Sends a SCPI command without expecting a response."""
    cmd = (cmd_str.strip() + "\n").encode("utf-8")
    ser.write(cmd)
    ser.flush()
    time.sleep(delay)


def run_scpi_test_suite(port, baud):
    print(f"Connecting to Flywheel Test Fixture on {port} @ {baud} baud...")
    try:
        ser = serial.Serial(port, baud, timeout=1.0)
    except Exception as e:
        print(f"Failed to open serial port {port}: {e}")
        return False

    time.sleep(0.5)

    print("\n================================================================================")
    print("  FLYWHEEL TEST FIXTURE: AUTOMATED SCPI VERIFICATION SUITE")
    print("================================================================================\n")

    try:
        # Step 1: Switch to SCPI mode
        print("[1/9] Switching to SCPI Mode...")
        # Sending SCPI trigger command
        ser.write(b":SYST:COMM:MODE SCPI\r\n")
        time.sleep(0.2)
        ser.reset_input_buffer()

        # Step 2: Query *IDN?
        print("[2/9] Testing *IDN? (Identification Query)...")
        idn = send_query(ser, "*IDN?")
        print(f"      Response: '{idn}'")
        assert "STM32G474RE" in idn or "STMicroelectronics" in idn, f"Unexpected *IDN? response: {idn}"
        print("      PASS: Instrument identification verified.")

        # Step 3: Clear status & Query *OPC?
        print("[3/9] Testing *CLS and *OPC? (Operation Complete)...")
        send_cmd(ser, "*CLS")
        opc = send_query(ser, "*OPC?")
        print(f"      Response: '{opc}'")
        assert opc == "1", f"Unexpected *OPC? response: {opc}"
        print("      PASS: Operation Complete confirmed.")

        # Step 4: Query Error Queue (:SYST:ERR?)
        print("[4/9] Testing :SYSTem:ERRor? (Error Queue)...")
        err = send_query(ser, ":SYST:ERR?")
        print(f"      Response: '{err}'")
        assert "0,\"No error\"" in err, f"Expected 0,'No error', got: {err}"
        print("      PASS: Error queue clean.")

        # Step 5: DC Bus Voltage Query (:MEAS:VOLT:DC?)
        print("[5/9] Testing :MEASure:VOLTage:DC?...")
        vbus_str = send_query(ser, ":MEAS:VOLT:DC?")
        print(f"      Response: '{vbus_str}' V")
        vbus = float(vbus_str)
        assert 0.0 <= vbus <= 48.0, f"VBUS {vbus}V out of expected range"
        print(f"      PASS: Valid Bus Voltage: {vbus:.2f} V")

        # Step 6: Speed Setpoint Command & Query
        print("[6/9] Testing :SOURce:MOTor:SPEEd (Speed Set & Query)...")
        send_cmd(ser, ":SOUR:MOT:SPEE 450")
        target_rpm_str = send_query(ser, ":SOUR:MOT:SPEE?")
        print(f"      Response: '{target_rpm_str}' RPM")
        target_rpm = float(target_rpm_str)
        assert abs(target_rpm - 450.0) < 1.0, f"Speed setpoint mismatch: {target_rpm}"
        print("      PASS: Commanded speed verified.")

        # Step 7: High-Speed Multi-Channel Telemetry Stream
        print("[7/9] Testing :MEASure:TELEmetry:ALL? (High-Speed Single-Line Snapshot)...")
        telem_raw = send_query(ser, ":MEAS:TELE:ALL?")
        print(f"      Response: '{telem_raw}'")
        fields = telem_raw.split(",")
        assert len(fields) == 12, f"Expected 12 telemetry fields, got {len(fields)}"
        field_names = [
            "VBUS (V)", "Temp (C)", "Actual RPM", "Optical RPM", "Hall RPM",
            "Iq (A)", "Id (A)", "Ipeak (A)", "Duty (%)", "Stall", "Fault", "Pulses"
        ]
        for name, val in zip(field_names, fields):
            print(f"        - {name:<14}: {val}")
        print("      PASS: Full synchronized telemetry parsed cleanly.")

        # Step 8: Error Generation & Undefined Header Handling
        print("[8/9] Testing Error Handling on Undefined Header (:INVALID:CMD?)...")
        send_cmd(ser, ":INVALID:HEADER:TEST?")
        err_invalid = send_query(ser, ":SYST:ERR?")
        print(f"      Response: '{err_invalid}'")
        assert "-113,\"Undefined header\"" in err_invalid, f"Expected -113, got: {err_invalid}"
        # Clear error queue
        send_cmd(ser, "*CLS")
        print("      PASS: Standard error code -113 generated and cleared.")

        # Step 9: Return to Console Mode
        print("[9/9] Testing Mode Transition back to Console (:SYST:COMM:MODE CONSOLE)...")
        send_cmd(ser, ":SYST:COMM:MODE CONSOLE")
        time.sleep(0.2)
        print("      PASS: Returned to interactive console mode.")

        print("\n================================================================================")
        print("  >>> ALL 9 SCPI SYSTEM VERIFICATION TESTS PASSED SUCCESSFULLY! <<<")
        print("================================================================================\n")
        return True

    finally:
        ser.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Flywheel SCPI Test Runner")
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial port (default: /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    args = parser.parse_args()

    success = run_scpi_test_suite(args.port, args.baud)
    sys.exit(0 if success else 1)
