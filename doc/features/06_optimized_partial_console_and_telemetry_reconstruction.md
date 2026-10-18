# Feature Specification: Differential Partial Console Refresh & Stream Reconstructor

## 1. Executive Summary

The evaluation console runs over LPUART1 at 115,200 baud (ST-LINK V3E Virtual COM Port, `/dev/ttyACM0`). In prior releases, `Console_RenderDashboard()` emitted a complete 80x28 text matrix (~2,400 bytes) every 250 ms (4 Hz). Transmitting 2,400 bytes consumed ~208 ms out of each 250 ms period—an **83.3% continuous link saturation**.

This feature introduces:
1. **Differential ANSI Partial Refresh Engine**: A static background template is drawn only on boot, mode switch, or manual redraw (`L` / `Ctrl-L`). Periodic 4 Hz refreshes emit only changed fields using fixed ANSI Cursor Positioning (`\x1b[<row>;<col>H`), reducing UART transmission volume to **~150–250 bytes/refresh** (an average **>90% bandwidth reduction** to <6% link duty cycle).
2. **Screen Session Capture Utility (`tools/capture_console.py`)**: A non-intrusive CLI manager to capture instant screen buffer hardcopy snapshots or stream raw logs from active GNU screen sessions on `/dev/pts/1`.
3. **Telemetry & Event Reconstructor (`tools/reconstruct_telemetry.py`)**: An analytical decoder that uses fixed cursor positioning sequences as parameter addresses and the incrementing **Sample #** as a master clock/epoch to reconstruct discrete event timelines and synchronized time-series CSV datasets.

---

## 2. Firmware Architecture & Coordinate Mapping

### 2.1 Fixed Screen Layout (78 Columns x 28 Rows)

To guarantee compatibility across standard 80-column serial terminals and prevent automatic line-wrapping from corrupting row positioning, all background borders and labels are constrained to $\le 78$ characters.

| Row | Column | Parameter / Field | Format & Description |
|---|---|---|---|
| **1** | 1 | Top Border | `==============================================================================` |
| **2** | 1 | Title Banner | `   STM32G474RE + <Board> -- BLDC MOTOR EVALUATION DASHBOARD` |
| **3** | 1 | Separator | `==============================================================================` |
| **4** | 1 | Link Info | ` Port   : LPUART1 (PA2/PA3) @ 115200 \| Clock: 170 MHz \| PWM: 20 kHz` |
| **5** | 11 | **Uptime** | `%02u:%02u:%02u` |
| **5** | 44 | **Sample # Anchor** | `Sample: #%07lu  [Heartbeat: %c]` *(Clock delimiter)* |
| **6** | 1 | Section Divider | `------------------------------------------------------------------------------` |
| **7** | 1 | Subtitle | ` POWER STAGE TELEMETRY (<Board> - <Driver>)` |
| **8** | 1 | Section Divider | `------------------------------------------------------------------------------` |
| **9** | 31 | **Bus Voltage (VBUS)** | `%5.2f V %-18s` (`[OK]`, `[LOW]`, `[OVERVOLTAGE]`) |
| **10** | 31 | **Driver Temperature** | `%4.1f deg C` |
| **11** | 31 | **Potentiometer (R42)** | `%5.1f %% [ACTIVE -> Target %4d RPM]` |
| **12** | 31 | **Driver Status** | `[READY / NORMAL]` or `[FAULT / OVERCURRENT TRIPPED]` |
| **13** | 31 | **Hall Signals** | `State %d [H1=%d, H2=%d, H3=%d]` or `[SENSORLESS]` |
| **14** | 1 | Section Divider | `------------------------------------------------------------------------------` |
| **15** | 1 | Subtitle | ` MOTOR DRIVE STATUS` |
| **16** | 1 | Section Divider | `------------------------------------------------------------------------------` |
| **17** | 31 | **Drive State** | `STOPPED`, `STARTING`, `RUNNING`, `ACTIVE BRAKING` |
| **18** | 31 | **Control Strategy** | `STAGE 1: OPEN-LOOP V/F (SENSORLESS)`, etc. |
| **19** | 31 | **Active Motor Profile** | `[%u/%u] %s (%u PP, Max %4.2f A)` |
| **20** | 31 | **Target & Actual Speed** | `Target %4d RPM \| Est %4d RPM (%s)` |
| **21** | 31 | **Commanded Torque Limit** | `%4.2f A (Software Regulated Ceiling)` |
| **22** | 31 | **Active Modulated Duty** | `%4.1f %% (Base %4.1f %% \| ARR = %u)` |
| **23** | 31 | **Phase Currents (mA)** | `IA=%4d mA \| IB=%4d mA \| IC=%4d mA [Pk=%4d mA]` |
| **24** | 31 | **HW Break Trip Limit** | `%.2f A (<Driver> OCD Break)` |
| **25** | 1 | Bottom Border | `==============================================================================` |
| **26** | 1 | Commands Line 1 | `  [SPACE] Run/Stop \| [+/-] Torque \| [P] Pot  \| [D] Dir \| [[/]] Duty \| [R] Reset` |
| **27** | 1 | Commands Line 2 | `  [O] Profile      \| [C] Char     \| [B] Brake\| [L] Redraw` |
| **28** | 1 | Bottom Border | `==============================================================================` |
| **29** | 1 | Cursor Park | Cursor parked off-screen to prevent visible blinking |

### 2.2 Differential State Cache

The firmware maintains a shadow cache (`MainShadow_t` and `CharShadow_t`) storing the most recently transmitted values. A field is only transmitted if:
1. `s_force_full_redraw` or `!s_main_shadow.valid` is set.
2. The numeric value exceeds the defined hysteresis threshold (e.g., $|\Delta V_{bus}| \ge 0.05\text{ V}$, $|\Delta T| \ge 0.2\text{ }^\circ\text{C}$, $|\Delta I| \ge 5\text{ mA}$, $|\Delta Speed| \ge 1\text{ RPM}$).
3. A state enumeration or discrete flag changes (`fault_active`, `state`, `pot_ctrl`).

The **Sample #** anchor (`Row 5, Col 44`) is emitted unconditionally on every 250 ms cycle, providing an exact heartbeat and frame boundary for external log parsers.

### 2.3 Immediate Unbuffered stdio Transmission

Because partial refresh updates purposefully omit newline characters (`\n`) to prevent vertical terminal scrolling, standard library `libc` (`newlib-nano`) would default to holding output until a 1,024-byte block accumulated (resulting in a ~3.0 second perceived freeze at ~340 B/s partial throughput). To guarantee real-time, zero-latency transmission:
1. `setvbuf(stdout, NULL, _IONBF, 0)` and `setvbuf(stderr, NULL, _IONBF, 0)` are invoked in `console_init()`.
2. An explicit `fflush(stdout)` is executed at the completion of every dashboard rendering pass.

---

## 3. Tooling Specifications

### 3.1 `tools/capture_console.py`

CLI tool to interact with the active GNU screen session without killing or attaching to the user's terminal:

```bash
# Take clean visible screen snapshot
python3 tools/capture_console.py --snapshot [output.txt]

# Trigger full screen repaint ('l')
python3 tools/capture_console.py --redraw

# Programmatically inject hotkeys into the console tty:
python3 tools/capture_console.py --key space      # [SPACE] Start / Stop motor
python3 tools/capture_console.py --key pot        # [P] Toggle potentiometer control
python3 tools/capture_console.py --key dir        # [D] Toggle CW/CCW direction
python3 tools/capture_console.py --key profile    # [O] Next motor profile
python3 tools/capture_console.py --key torque_up  # [+] Increment torque limit
python3 tools/capture_console.py --key torque_down# [-] Decrement torque limit
python3 tools/capture_console.py --key char       # [C] Start characterization
python3 tools/capture_console.py --key brake      # [B] Active brake
python3 tools/capture_console.py --key reset      # [R] Reset fault
python3 tools/capture_console.py --send "any text"# Send arbitrary string/keystroke

# Start background stream logging to file
python3 tools/capture_console.py --start-log telemetry.log

# Stop stream logging
python3 tools/capture_console.py --stop-log

# Timed capture with initial redraw
python3 tools/capture_console.py --capture-duration 10 --redraw -o telemetry_10s.log
```

### 3.2 `tools/reconstruct_telemetry.py`

Parser and analytical reconstructor that turns raw ANSI stream bytes into structured data:

```bash
# Reconstruct timeline of events and print summary
python3 tools/reconstruct_telemetry.py telemetry.log --events

# Export synchronized tabular CSV time-series
python3 tools/reconstruct_telemetry.py telemetry.log --csv telemetry.csv
```

#### CSV Export Schema:
- `sample_num`: Hardware telemetry sample index
- `uptime`: Elapsed Nucleo run time (`HH:MM:SS`)
- `vbus_v`: Measured DC bus voltage (V)
- `temp_c`: Measured driver temperature (°C)
- `pot_pct`: Target speed potentiometer angle (%)
- `driver_fault`: Boolean overcurrent trip flag
- `motor_state`: Operational drive state
- `control_mode`: Active commutation strategy
- `profile_name`: Active motor profile
- `target_rpm`: Target shaft speed (RPM)
- `est_rpm`: Estimated/measured shaft speed (RPM)
- `direction`: Shaft rotation direction (`CW`/`CCW`)
- `torque_limit_amps`: Commanded peak current ceiling (A)
- `active_duty_pct`: Voltage-feedforward modulated duty cycle (%)
- `base_duty_pct`: Unmodulated base duty cycle (%)
- `ia_ma`, `ib_ma`, `ic_ma`: Phase currents (mA)
- `ipk_ma`: Peak envelope current (mA)
- `hw_limit_amps`: Hardware comparator trip reference (A)

---

## 4. Bandwidth & Performance Benchmark

| Metric | Legacy Full Refresh | Optimized Partial Refresh | Improvement |
|---|---|---|---|
| **Payload per Refresh** | ~2,400 bytes | ~120 – 180 bytes (steady) | **92.5% – 95% reduction** |
| **UART Link Duty Cycle** | 83.3% @ 115200 baud | 4.2% – 6.2% @ 115200 baud | **>13x reduction** |
| **Transmission Duration** | ~208 ms / 250 ms | ~10 – 16 ms / 250 ms | **>13x faster** |
| **Terminal Cursor Tearing** | Noticeable flickering | Zero flicker (smooth) | **Completely eliminated** |
| **Log Storage Overhead** | ~576 KB / minute | ~24 – 35 KB / minute | **16x smaller logfiles** |
