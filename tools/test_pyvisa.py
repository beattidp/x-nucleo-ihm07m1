#!/usr/bin/env python3
"""
PyVISA Automated Verification & Control Script for Flywheel Test Fixture

Validates SCPI communication over the ST-LINK V3E Virtual COM Port
using PyVISA and the pyvisa-py backend.

Usage:
    python3 tools/test_pyvisa.py [--resource ASRL/dev/ttyACM0::INSTR] [--baud 115200]
"""

import argparse
import sys
import time

try:
    import pyvisa
except ImportError:
    print("Error: PyVISA is required. Install via: pip install -r requirements.txt")
    sys.exit(1)


def run_pyvisa_test(resource_name, baud_rate):
    print(f"Initializing PyVISA ResourceManager with '@py' backend...")
    try:
        rm = pyvisa.ResourceManager("@py")
    except Exception as e:
        print(f"Failed to initialize PyVISA '@py' backend: {e}")
        print("Ensure pyvisa-py and pyserial are installed (pip install -r requirements.txt).")
        return False

    print(f"Connecting to instrument: {resource_name} @ {baud_rate} baud...")
    try:
        inst = rm.open_resource(resource_name, baud_rate=baud_rate)
    except Exception as e:
        print(f"\n[ERROR] Could not open resource {resource_name}: {e}")
        if "Device or resource busy" in str(e):
            print("\nHint: /dev/ttyACM0 is currently held open by another process.")
            print("Check for active screen/minicom sessions (e.g. `fuser /dev/ttyACM0` or `screen -ls`)")
            print("and detach or exit them before running PyVISA automation.")
        return False

    # Standard SCPI termination configuration
    inst.read_termination = "\n"
    inst.write_termination = "\n"
    inst.timeout = 2000  # 2000 ms timeout

    print("\n================================================================================")
    print("  FLYWHEEL TEST FIXTURE: PYVISA VERIFICATION SUITE")
    print("================================================================================\n")

    try:
        # Step 1: Switch from interactive ANSI console to SCPI protocol mode
        print("[1/8] Setting communication mode to SCPI...")
        inst.write(":SYST:COMM:MODE SCPI")
        time.sleep(0.1)

        # Step 2: Query IEEE 488.2 Identification
        print("[2/8] Querying *IDN? (Identification Query)...")
        idn = inst.query("*IDN?")
        print(f"      Response: '{idn}'")
        assert "STM32G474RE" in idn or "STMicroelectronics" in idn, f"Unexpected *IDN?: {idn}"
        print("      PASS: Instrument identification confirmed.")

        # Step 3: Clear Status and Check Operation Complete
        print("[3/8] Testing *CLS and *OPC?...")
        inst.write("*CLS")
        opc = inst.query("*OPC?")
        print(f"      Response: '{opc}'")
        assert opc == "1", f"Unexpected *OPC? response: {opc}"
        print("      PASS: Operation Complete confirmed.")

        # Step 4: Query SCPI Error Queue
        print("[4/8] Querying :SYSTem:ERRor?...")
        err = inst.query(":SYST:ERR?")
        print(f"      Response: '{err}'")
        assert "0,\"No error\"" in err, f"Expected 0,'No error', got: {err}"
        print("      PASS: Error queue clean.")

        # Step 5: Measure DC Bus Voltage
        print("[5/8] Querying :MEASure:VOLTage:DC?...")
        vbus_str = inst.query(":MEAS:VOLT:DC?")
        print(f"      Response: {vbus_str} V")
        vbus = float(vbus_str)
        assert 0.0 <= vbus <= 48.0, f"VBUS {vbus}V out of expected range"
        print(f"      PASS: Valid Bus Voltage: {vbus:.2f} V")

        # Step 6: Commanded Speed Setpoint & Query
        print("[6/8] Setting & Querying :SOURce:MOTor:SPEEd (450 RPM)...")
        inst.write(":SOUR:MOT:SPEE 450")
        target_rpm_str = inst.query(":SOUR:MOT:SPEE?")
        print(f"      Response: {target_rpm_str} RPM")
        target_rpm = float(target_rpm_str)
        assert abs(target_rpm - 450.0) < 1.0, f"Speed setpoint mismatch: {target_rpm}"
        print("      PASS: Commanded speed verified.")

        # Step 7: Multi-channel synchronized telemetry snapshot
        print("[7/8] Querying :MEASure:TELEmetry:ALL? (High-Speed Synchronized Snapshot)...")
        telem_raw = inst.query(":MEAS:TELE:ALL?")
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

        # Step 8: Return to ANSI Console Mode
        print("[8/8] Restoring interactive console mode (:SYST:COMM:MODE CONSOLE)...")
        inst.write(":SYST:COMM:MODE CONSOLE")
        time.sleep(0.1)
        print("      PASS: Console mode restored.")

        print("\n================================================================================")
        print("  >>> ALL PYVISA VERIFICATION TESTS PASSED SUCCESSFULLY! <<<")
        print("================================================================================\n")
        return True

    finally:
        inst.close()
        rm.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Flywheel PyVISA Test Runner")
    parser.add_argument(
        "--resource",
        default="ASRL/dev/ttyACM0::INSTR",
        help="VISA resource string (default: ASRL/dev/ttyACM0::INSTR)",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)",
    )
    args = parser.parse_args()

    success = run_pyvisa_test(args.resource, args.baud)
    sys.exit(0 if success else 1)
