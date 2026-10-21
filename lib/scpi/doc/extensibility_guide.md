# Developer Guide: Adding SCPI Commands to Any Test Fixture

This guide demonstrates how to define, register, and verify custom SCPI commands and queries using `lib/scpi` in any microcontroller project.

---

## 1. Five-Minute Quickstart

Adding a new command involves three simple steps:

### Step 1: Write the Handler Function

Handlers take a `ScpiContext& ctx` reference and return `SCPI_RES_OK` or `SCPI_RES_ERR`:

```cpp
#include "scpi/scpi_parser.h"

// Command & Query handler for setting/reading voltage
static scpi::ScpiStatus handle_voltage(scpi::ScpiContext& ctx) {
    if (ctx.isQuery()) {
        // Format response (query ended with '?')
        float v_actual = Hardware_ReadVoltage();
        ctx.replyFloat(v_actual, 2); // Emits e.g. "12.45"
    } else {
        // Extract parameter with automatic type and range checking
        float v_target = 0.0f;
        if (ctx.getParamFloat(v_target, 0.0f, 36.0f)) {
            Hardware_SetVoltage(v_target);
        }
        // If out of range, ctx.getParamFloat automatically logs:
        // -222, "Data out of range" to the SCPI error queue!
    }
    return scpi::SCPI_RES_OK;
}
```

### Step 2: Declare the Command in the Table

Add an entry to your fixture's command table:

```cpp
static const scpi::ScpiCommandDef s_my_fixture_commands[] = {
    // Pattern                       Flags                       Handler
    { "SOURce:VOLTage[:LEVel]",       scpi::SCPI_CMD_AND_QUERY,  &handle_voltage },
    { "OUTPut[:STATe]",               scpi::SCPI_CMD_AND_QUERY,  &handle_output_state },
    { "MEASure:CURRent[:DC]?",        scpi::SCPI_QUERY_ONLY,     &handle_meas_current },
};
```

### Step 3: Register the Table during Initialization

```cpp
scpi::ScpiParser parser(my_io_transport);
parser.registerCommands(s_my_fixture_commands, 
                       sizeof(s_my_fixture_commands) / sizeof(s_my_fixture_commands[0]));
```

---

## 2. Parameter Extraction Reference

The `ScpiContext` class provides robust, zero-copy parameter helpers:

| Helper Method | Arguments | Behavior | Error Code on Failure |
| :--- | :--- | :--- | :--- |
| `ctx.getParamFloat(out, min, max)` | `float& out, float min, float max` | Parses floating-point (including exponential `1.5e-3`), validates within `[min, max]`. | `-104 Data type error` or `-222 Data out of range` |
| `ctx.getParamInt(out, min, max)` | `int32_t& out, int32_t min, int32_t max` | Parses integer (decimal, hex `0x...`, octal `0...`), validates within `[min, max]`. | `-104 Data type error` or `-222 Data out of range` |
| `ctx.getParamBool(out)` | `bool& out` | Matches `1`, `0`, `ON`, `OFF` (case-insensitive). | `-104 Data type error` |
| `ctx.getParamEnum(out_idx, choices, count)` | `int& out_idx, const char* const* choices, size_t count` | Matches token against array of string keywords. | `-104 Data type error` |
| `ctx.getParamString(out_buf, max_len)` | `char* out_buf, size_t max_len` | Extracts string, automatically stripping quotes (`"..."` or `'...'`). | `-109 Missing parameter` |

---

## 3. Response Formatting Reference

| Helper Method | Example Call | Produced Response |
| :--- | :--- | :--- |
| `ctx.replyFloat(val, decimals)` | `ctx.replyFloat(1450.25f, 1)` | `1450.3` |
| `ctx.replyInt(val)` | `ctx.replyInt(42)` | `42` |
| `ctx.replyBool(val)` | `ctx.replyBool(true)` | `1` |
| `ctx.replyString(str)` | `ctx.replyString("RUNNING")` | `RUNNING` |
| `ctx.replyPrintf(fmt, ...)` | `ctx.replyPrintf("%.2f,%.2f", ia, ib)` | `0.45,0.48` |

> [!NOTE]
> All query responses are automatically terminated with `\r\n` upon query completion and flushed to the transport stream.

---

## 4. Pattern Syntax Rules

1. **Mandatory vs. Optional Letters**:
   - Uppercase letters in the pattern string define the **short form**.
   - Lowercase letters define the optional remainder of the **long form**.
   - Example: `CURRent` accepts `CURR` or `CURRENT`.
2. **Optional Subtree Nodes (`[:NODE]`)**:
   - Enclose optional hierarchy levels in brackets.
   - Example: `OUTPut[:STATe]` matches both `OUTP` and `OUTP:STAT`.
3. **Flags**:
   - `SCPI_CMD_ONLY`: Command cannot be queried with `?` (e.g. `*RST`, `Motor_ResetFault`).
   - `SCPI_QUERY_ONLY`: Can only be queried; sending as a command without `?` generates `-113`.
   - `SCPI_CMD_AND_QUERY`: Valid as both setting and interrogation.
