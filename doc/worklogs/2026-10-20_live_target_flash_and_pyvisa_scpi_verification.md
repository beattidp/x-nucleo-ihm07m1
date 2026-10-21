# Worklog: Target Firmware Flash & Live PyVISA / SCPI Hardware Verification

- **Date**: 2026-10-20
- **Time**: 21:44 MDT
- **Agent**: Antigravity Pair Programmer
- **User**: Dan Beattie

---

## 1. Summary of Actions
- **Flashed Target Hardware via ST-Link**:
  - Rebuilt and flashed firmware to STM32G474RE Nucleo board using `pio run -t upload`.
  - Flash: 72416 bytes (13.8%), RAM: 8140 bytes (6.2%). Verified clean flash programming and target MCU reset.
- **Identified & Resolved SCPI Hierarchical System Subtree Fallthrough Bug**:
  - Root Cause: In `ScpiParser::executeSubcommand()`, headers beginning with `:SYST` were unconditionally dispatched to `handleSystemSubtree()`, which returned `void` and emitted `-113,"Undefined header"` on unknown `:SYST` commands without checking `m_commands`. This prevented user-registered system extensions such as `:SYSTem:COMMunication:MODE` from executing.
  - Resolution: Updated `handleSystemSubtree()` to return `bool` (`true` if handled by built-in system commands like `:SYST:ERR?`, `:SYST:VERS?`; `false` otherwise). Allowed non-built-in system headers to fall through to the device-specific command table `m_commands`.
  - Verified off-target unit tests (`make -C lib/scpi/test`): All 10 test cases passed.
- **Enhanced `tools/test_pyvisa.py`**:
  - Added robust buffer flushing (`inst.flush(BufferType.io_in)`) after switching to SCPI mode to drain in-flight ANSI console escape sequences.
  - Configured CRLF line termination (`\r\n`) and added response string trimming.
- **Executed Live Hardware Verification**:
  - Ran `python3 tools/test_pyvisa.py`: 8/8 tests passed via pure-Python PyVISA backend (`@py`) over `ASRL/dev/ttyACM0::INSTR` @ 115200 baud.
  - Ran `python3 tools/test_scpi.py`: 9/9 tests passed via PySerial.
  - Verified bidirectional mode switching:
    1. Interactive Console mode streams 4 Hz ANSI dashboard updates.
    2. Host sends `:SYST:COMM:MODE SCPI` (or `*`/`:` prefix); firmware mutes dashboard rendering and enters deterministic SCPI transaction mode.
    3. Host queries `*IDN?`, `*OPC?`, `:MEAS:VOLT:DC?`, `:SOUR:MOT:SPEE`, `:MEAS:TELE:ALL?` with zero transmission collisions.
    4. Host sends `:SYST:COMM:MODE CONSOLE`; firmware immediately restores 4 Hz live console rendering.
