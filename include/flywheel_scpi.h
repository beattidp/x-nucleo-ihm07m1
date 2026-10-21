#ifndef __FLYWHEEL_SCPI_H
#define __FLYWHEEL_SCPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "console.h"

/**
 * @brief Initialize the SCPI engine with Flywheel Test Fixture command bindings
 */
void FlywheelScpi_Init(void);

/**
 * @brief Process incoming characters from serial ring buffer when SCPI mode is active
 */
void FlywheelScpi_Process(void);

/**
 * @brief Switch active communication mode (COMM_MODE_CONSOLE or COMM_MODE_SCPI)
 */
void FlywheelScpi_SetMode(CommMode_t mode);

/**
 * @brief Query active communication mode
 */
CommMode_t FlywheelScpi_GetMode(void);

#ifdef __cplusplus
}
#endif

#endif /* __FLYWHEEL_SCPI_H */
