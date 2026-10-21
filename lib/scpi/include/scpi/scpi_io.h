#ifndef __SCPI_IO_H
#define __SCPI_IO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
namespace scpi {

/**
 * @brief Abstract Stream Transport Interface for SCPI Communication
 *
 * Implements non-blocking read/write operations. Decouples the parser engine
 * from physical communication hardware (UART, USB-CDC, Ethernet, TCP/IP, or mock buffers).
 */
class IScpiIo {
public:
    virtual ~IScpiIo() = default;

    /**
     * @brief Writes raw bytes to the communication stream.
     * @param data Pointer to buffer
     * @param len Number of bytes to transmit
     * @return Number of bytes actually written
     */
    virtual int write(const uint8_t* data, size_t len) = 0;

    /**
     * @brief Transmits a single character.
     * @param c Character to send
     * @return 1 on success, 0 on failure
     */
    virtual int putc(char c) = 0;

    /**
     * @brief Reads a single character from the receive buffer.
     * @return Character byte (0..255), or -1 if no character is available.
     */
    virtual int getc() = 0;

    /**
     * @brief Checks if receive data is pending.
     */
    virtual bool hasRx() const = 0;

    /**
     * @brief Flushes any queued transmit data.
     */
    virtual void flush() = 0;
};

} /* namespace scpi */
#endif /* __cplusplus */

#endif /* __SCPI_IO_H */
