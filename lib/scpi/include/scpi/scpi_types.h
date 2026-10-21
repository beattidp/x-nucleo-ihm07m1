#ifndef __SCPI_TYPES_H
#define __SCPI_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
namespace scpi {
#endif

/* SCPI Execution Status */
typedef enum {
    SCPI_RES_OK = 0,
    SCPI_RES_ERR = -1
} ScpiStatus;

/* Standard SCPI Error Codes (SCPI-99 / IEEE 488.2) */
typedef enum {
    SCPI_ERR_NONE                     = 0,
    SCPI_ERR_SYNTAX                   = -100, /* Command error: Generic syntax error */
    SCPI_ERR_INVALID_SEPARATOR        = -101, /* Invalid separator */
    SCPI_ERR_DATA_TYPE                = -104, /* Data type error */
    SCPI_ERR_PARAMETER_NOT_ALLOWED    = -108, /* Parameter not allowed */
    SCPI_ERR_MISSING_PARAMETER        = -109, /* Missing parameter */
    SCPI_ERR_UNDEFINED_HEADER         = -113, /* Undefined header */
    SCPI_ERR_EXECUTION_ERROR          = -200, /* Generic execution error */
    SCPI_ERR_DATA_OUT_OF_RANGE        = -222, /* Data out of range */
    SCPI_ERR_HARDWARE_ERROR           = -240, /* Hardware error */
    SCPI_ERR_QUEUE_OVERFLOW           = -350, /* Queue overflow */
    SCPI_ERR_QUERY_INTERRUPTED        = -410, /* Query INTERRUPTED */
    SCPI_ERR_QUERY_UNTERMINATED       = -420  /* Query UNTERMINATED */
} ScpiErrorCode;

/* IEEE 488.2 Status Byte (STB) Bitmasks */
#define SCPI_STB_ERROR_QUEUE_NOT_EMPTY  (1U << 2) /* Bit 2: Error/Event Queue not empty */
#define SCPI_STB_QUESTIONABLE_SUMMARY   (1U << 3) /* Bit 3: Questionable status summary */
#define SCPI_STB_MESSAGE_AVAILABLE      (1U << 4) /* Bit 4: Message Available (MAV) */
#define SCPI_STB_EVENT_STATUS_SUMMARY   (1U << 5) /* Bit 5: Standard Event Status Summary (ESB) */
#define SCPI_STB_MASTER_SUMMARY_STATUS  (1U << 6) /* Bit 6: Master Summary Status (MSS) / RQS */
#define SCPI_STB_OPERATION_SUMMARY      (1U << 7) /* Bit 7: Operation status summary */

/* IEEE 488.2 Standard Event Status Register (ESR) Bitmasks */
#define SCPI_ESR_OPERATION_COMPLETE     (1U << 0) /* Bit 0: OPC */
#define SCPI_ESR_QUERY_ERROR            (1U << 2) /* Bit 2: QYE */
#define SCPI_ESR_DEVICE_DEPENDENT_ERROR (1U << 3) /* Bit 3: DDE */
#define SCPI_ESR_EXECUTION_ERROR        (1U << 4) /* Bit 4: EXE */
#define SCPI_ESR_COMMAND_ERROR          (1U << 5) /* Bit 5: CME */
#define SCPI_ESR_USER_REQUEST           (1U << 6) /* Bit 6: URQ */
#define SCPI_ESR_POWER_ON               (1U << 7) /* Bit 7: PON */

/* Command Flags */
#define SCPI_CMD_ONLY                   0x01U
#define SCPI_QUERY_ONLY                 0x02U
#define SCPI_CMD_AND_QUERY              0x03U

#ifdef __cplusplus
} /* namespace scpi */
#endif

#endif /* __SCPI_TYPES_H */
