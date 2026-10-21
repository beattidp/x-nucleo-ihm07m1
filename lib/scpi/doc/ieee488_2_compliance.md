# IEEE 488.2 & SCPI-99 Compliance Specification

This document details the compliance matrix, register models, and standard error handling implemented by `lib/scpi`.

---

## 1. Mandatory IEEE 488.2 Common Commands

`lib/scpi` implements all 13 core mandatory commands and queries specified by Section 10 of the IEEE 488.2 standard:

| Command | Syntax | Type | Compliance Description |
| :--- | :--- | :--- | :--- |
| `*IDN?` | `*IDN?` | Query | Returns 4 comma-separated fields: `<Manufacturer>,<Model>,<Serial>,<Firmware>`. Configured via `ScpiParser::setIdn()`. |
| `*RST` | `*RST` | Command | Resets device to known safe defaults. Invokes registered `ResetCallback`. Clears temporary execution state; preserves error queue. |
| `*CLS` | `*CLS` | Command | Clears the Error Queue and clears all event registers (`ESR`, Questionable, Operation). |
| `*OPC` | `*OPC` | Command | Sets Bit 0 (Operation Complete) in the Standard Event Status Register (`ESR`) upon completion of pending operations. |
| `*OPC?` | `*OPC?` | Query | Returns ASCII `1\r\n` when all pending operations finish. Used by host test scripts for synchronization. |
| `*STB?` | `*STB?` | Query | Reads the Status Byte Register (`STB`) as a decimal integer (`0..255`). Does not clear the STB. |
| `*ESR?` | `*ESR?` | Query | Reads the Standard Event Status Register (`ESR`) as a decimal integer (`0..255`) and **clears** it to 0. |
| `*ESE` | `*ESE <0..255>` | Command | Sets the Event Status Enable Register mask. |
| `*ESE?` | `*ESE?` | Query | Returns the current Event Status Enable Register mask. |
| `*SRE` | `*SRE <0..255>` | Command | Sets the Service Request Enable Register mask. |
| `*SRE?` | `*SRE?` | Query | Returns the current Service Request Enable Register mask. |
| `*TST?` | `*TST?` | Query | Executes self-test callback. Returns `0` for Pass, or non-zero error code for failure. |
| `*WAI` | `*WAI` | Command | Wait-to-continue. Synchronous execution guarantee on single-threaded embedded targets. |

---

## 2. Status Reporting Architecture

```text
               +-------------------------------------------------------+
               | Standard Event Status Register (ESR) (*ESR?)          |
               |  Bit 7: Power On (PON)                                |
               |  Bit 6: User Request (URQ)                            |
               |  Bit 5: Command Error (CME)                           |
               |  Bit 4: Execution Error (EXE)                         |
               |  Bit 3: Device Dependent Error (DDE)                  |
               |  Bit 2: Query Error (QYE)                             |
               |  Bit 0: Operation Complete (OPC)                      |
               +---------------------------+---------------------------+
                                           |
                                  AND with ESE mask (*ESE)
                                           |
                                           v  (Bit 5: ESB)
               +-------------------------------------------------------+
               | Status Byte Register (STB) (*STB?)                    |
               |  Bit 7: Operation Summary (OSB)                       |
               |  Bit 6: Master Summary Status (MSS) / RQS             |
               |  Bit 5: Event Status Bit (ESB) <----------------------+
               |  Bit 4: Message Available (MAV)                       |
               |  Bit 3: Questionable Summary (QSB)                    |
               |  Bit 2: Error Queue Not Empty (ERR) <---+             |
               +-----------------------------------------|-------------+
                                                         |
                                            Set when error queue count > 0
```

### Automatic Register Transitions
- When any command error (code `-100..-199`) occurs: `ESR` Bit 5 (`CME`) is set.
- When an execution error (code `-200..-299`) occurs: `ESR` Bit 4 (`EXE`) is set.
- When a query error (code `-400..-499`) occurs: `ESR` Bit 2 (`QYE`) is set.
- When any error is pushed to the queue: `STB` Bit 2 (`ERR`) is asserted.
- When `(ESR & ESE) != 0`: `STB` Bit 5 (`ESB`) is asserted.
- When `(STB & SRE) != 0`: `STB` Bit 6 (`MSS`) is asserted.

---

## 3. Standard SCPI Error Codes

The built-in error queue implements a 16-entry FIFO adhering to SCPI-99 syntax:

| Error Code | Error Message | Description / Trigger Condition |
| :---: | :--- | :--- |
| `0` | `"No error"` | Returned by `:SYST:ERR?` when the error queue is empty. |
| `-100` | `"Command error"` | Generic syntax error or corrupted command stream. |
| `-101` | `"Invalid separator"` | Misplaced delimiter or semicolon. |
| `-104` | `"Data type error"` | Parameter format mismatch (e.g. string passed when float expected). |
| `-108` | `"Parameter not allowed"` | Command received unexpected trailing arguments. |
| `-109` | `"Missing parameter"` | Required parameter was not supplied. |
| `-113` | `"Undefined header"` | Header does not match any registered pattern or query syntax violated. |
| `-200` | `"Execution error"` | Command recognized, but could not be executed by current hardware state. |
| `-222` | `"Data out of range"` | Parameter numeric value exceeds configured `[min, max]` boundaries. |
| `-240` | `"Hardware error"` | Physical driver fault, overcurrent trip, or diagnostic line asserted. |
| `-350` | `"Queue overflow"` | Error queue capacity (16) exceeded. Replaces the 16th queue entry. |
| `-410` | `"Query INTERRUPTED"` | New command arrived before previous query response was read. |
| `-420` | `"Query UNTERMINATED"` | Controller attempted to read data without sending a query first. |
