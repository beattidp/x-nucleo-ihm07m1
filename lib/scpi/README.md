# Reusable Embedded SCPI Parser (`lib/scpi`)

A zero-heap, object-oriented C++14 SCPI (Standard Commands for Programmable Instruments) parser designed for embedded microcontrollers, test benches, and laboratory instruments.

## Features

- **Zero Runtime Heap Allocation**: Safe for deterministic real-time motor control loops (20 kHz PWM / 1 kHz FOC).
- **Standards-Compliant SCPI-99 & IEEE 488.2**: Mandatory common commands (`*IDN?`, `*RST`, `*CLS`, `*OPC`, `*OPC?`, `*STB?`, `*ESR?`, `*ESE`, `*SRE`, `*TST?`, `*WAI`), status registers, and a 16-slot FIFO error queue (`:SYST:ERR?`).
- **Transport Abstraction (`IScpiIo`)**: Hardware-independent stream interface (works over LPUART, USB-CDC, Ethernet, or mock test streams).
- **Declarative Extensibility**: Add custom fixture commands in minutes with automated short/long form matching (3/4 letter rule) and type-safe parameter converters.
- **C Linkable (`extern "C"`)**: Easily integrated into existing C firmware codebases.

## Documentation

- [Architecture & Design Specification](doc/architecture.md)
- [IEEE 488.2 & SCPI-99 Compliance Matrix](doc/ieee488_2_compliance.md)
- [Developer Extensibility Guide](doc/extensibility_guide.md)
- [Operator User Guide & Refresher](doc/user_guide.md)

## Building Host Unit Tests

```bash
cd test
make
```
