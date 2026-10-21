# SCPI User Guide & Operational Refresher

## 1. Introduction

SCPI (Standard Commands for Programmable Instruments) provides a human-readable, standardized syntax for communicating with laboratory instruments and embedded test fixtures.

This guide provides an operational refresher on SCPI formatting, interactive terminal usage, and host automation with Python.

---

## 2. Command Syntax Fundamentals

### Hierarchical Tree Structure
SCPI commands are arranged hierarchically, separated by colons (`:`):
```text
:SOURce:MOTor:SPEEd 1500
  │      │     │     │
  │      │     │     └─ Parameter (1500 RPM)
  │      │     └─────── Subsystem Command
  │      └───────────── Subsystem
  └──────────────────── Root System
```

### Short Form vs. Long Form
Commands can be sent in either 3/4-character short form or full long form (case-insensitive):
- Short: `SOUR:MOT:SPEE 1500`
- Long: `SOURCE:MOTOR:SPEED 1500`
- Mixed Case: `sour:mot:spee 1500`

### Commands vs. Queries
- **Commands (Settings)** change device state and accept parameters:
  `SOUR:MOT:SPEE 1200`
- **Queries (Interrogations)** end with a question mark (`?`) and return data:
  `SOUR:MOT:SPEE?` $\to$ Returns `1200.0\r\n`
  `MEAS:VOLT:DC?`  $\to$ Returns `16.02\r\n`

### Semicolon Concatenation
Multiple commands can be chained on a single line separated by semicolons (`;`):
```text
*CLS;SOUR:MOT:SPEE 800;SOUR:MOT:DIR CW;*OPC?
```
This resets the error state, sets speed to 800 RPM, sets direction clockwise, and returns `1` when complete.

---

## 3. Interactive Serial Testing (picocom / screen / PuTTY)

Connect to the virtual COM port at **115200 baud, 8N1**:

```bash
picocom -b 115200 /dev/ttyACM0 --omap crlf --echo
```

### Essential Diagnostic Sequence:

1. **Verify Instrument Identification**:
   ```text
   *IDN?
   ```
   *Expected Response:* `STMicroelectronics,X-NUCLEO-IHM07M1-STM32G474RE,0,v1.0.0`

2. **Read Error Queue**:
   ```text
   :SYST:ERR?
   ```
   *Expected Response:* `0,"No error"`

3. **Check Status Byte**:
   ```text
   *STB?
   ```

4. **Return to Interactive Console Mode**:
   ```text
   :SYST:COMM:MODE CONSOLE
   ```

---

## 4. Host Automation with Python (PyVISA & PySerial)

### PySerial Example:
```python
import serial
import time

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1.0)

# Switch to SCPI mode if coming from console
ser.write(b"SCPI\r\n")
time.sleep(0.1)
ser.reset_input_buffer()

# Query Instrument ID
ser.write(b"*IDN?\n")
print("Instrument:", ser.readline().decode().strip())

# Read bus voltage
ser.write(b":MEAS:VOLT:DC?\n")
print("VBUS:", ser.readline().decode().strip(), "V")

# Start motor at 1000 RPM
ser.write(b":SOUR:MOT:SPEE 1000\n")
ser.write(b":OUTP ON\n")
ser.write(b"*OPC?\n")
assert ser.readline().decode().strip() == "1"

# Query high-speed telemetry snapshot
ser.write(b":MEAS:TELE:ALL?\n")
telem = ser.readline().decode().strip().split(",")
print("Telemetry Snapshot:", telem)

# Stop and return to console
ser.write(b":OUTP OFF\n")
ser.write(b":SYST:COMM:MODE CONSOLE\n")
ser.close()
```

### PyVISA Example (via `pyvisa-py`):
```python
import pyvisa
import time

# Initialize pure-python VISA backend
rm = pyvisa.ResourceManager("@py")

# Connect to ST-Link Virtual COM port at 115200 baud
inst = rm.open_resource("ASRL/dev/ttyACM0::INSTR", baud_rate=115200)
inst.read_termination = "\n"
inst.write_termination = "\n"
inst.timeout = 2000

# Switch to SCPI mode
inst.write(":SYST:COMM:MODE SCPI")
time.sleep(0.1)

# Query identification & DC bus voltage
print("Instrument:", inst.query("*IDN?"))
print("VBUS:", inst.query(":MEAS:VOLT:DC?"), "V")

# Command speed & read synchronized telemetry snapshot
inst.write(":SOUR:MOT:SPEE 1000")
inst.write(":OUTP ON")
telem = inst.query(":MEAS:TELE:ALL?").split(",")
print("Telemetry Snapshot (12 channels):", telem)

# Stop and restore console mode
inst.write(":OUTP OFF")
inst.write(":SYST:COMM:MODE CONSOLE")
inst.close()
rm.close()
```

