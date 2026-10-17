#ifndef __CONSOLE_H
#define __CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdio.h>
#include <stdbool.h>

#define CONSOLE_TX_BUF_SIZE         4096U
#define CONSOLE_RX_BUF_SIZE         256U

/* Forward declaration of telemetry and control structs */
struct MotorTelemetry_s;
struct MotorControl_s;

void console_init(void);
int console_write(const uint8_t *data, uint16_t len);
int console_putc(char c);
int console_getch(void);
bool console_has_char(void);
uint16_t console_tx_queue_len(void);

/* Syscall hooks for standard newlib printf / scanf */
int _write(int file, char *ptr, int len);
int _read(int file, char *ptr, int len);

/* Dashboard rendering */
void Console_RenderDashboard(const struct MotorTelemetry_s* telem, const struct MotorControl_s* ctrl, char heartbeat);
void Console_RenderCharDashboard(const struct MotorTelemetry_s* telem, const struct MotorControl_s* ctrl, char heartbeat);

#ifdef __cplusplus
}
#endif

#endif /* __CONSOLE_H */
