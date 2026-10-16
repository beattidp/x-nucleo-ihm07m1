#ifndef __CONSOLE_H
#define __CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "printf.h"

/* Forward declaration of telemetry and control structs */
struct MotorTelemetry_s;
struct MotorControl_s;

void console_init(void);
void __io_putc(void* p, char c);
int console_getch(void);
bool console_has_char(void);

/* Dashboard rendering */
void Console_RenderDashboard(const struct MotorTelemetry_s* telem, const struct MotorControl_s* ctrl, char heartbeat);

#ifdef __cplusplus
}
#endif

#endif /* __CONSOLE_H */
