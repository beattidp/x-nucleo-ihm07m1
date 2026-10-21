#ifndef __SCPI_ERROR_QUEUE_H
#define __SCPI_ERROR_QUEUE_H

#include "scpi_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
namespace scpi {

#define SCPI_ERROR_QUEUE_CAPACITY 16
#define SCPI_ERROR_MSG_MAX_LEN    48

struct ScpiError {
    int16_t code;
    char message[SCPI_ERROR_MSG_MAX_LEN];
};

/**
 * @brief Zero-Allocation Fixed-Capacity Circular Error FIFO Queue
 *
 * Implements SCPI-99 standard error queue semantics:
 * - Popping when empty returns 0, "No error".
 * - When queue overflows, the last slot is locked to -350, "Queue overflow".
 * - FIFO ordering for unread errors.
 */
class ScpiErrorQueue {
public:
    ScpiErrorQueue();

    /**
     * @brief Pushes an error code into the FIFO queue.
     * @param code Standard SCPI error code (e.g. -113)
     * @param custom_msg Optional message string (uses standard text if null)
     */
    void push(int16_t code, const char* custom_msg = nullptr);

    /**
     * @brief Pops the oldest error from the FIFO queue.
     * @param out Reference to receive the popped error
     * @return true if an actual error was popped, false if queue was empty (returns 0, "No error")
     */
    bool pop(ScpiError& out);

    /**
     * @brief Clears all errors from the queue.
     */
    void clear();

    /**
     * @brief Returns number of unread errors in the queue.
     */
    size_t count() const;

    /**
     * @brief Checks if queue is empty.
     */
    bool isEmpty() const;

    /**
     * @brief Returns standard textual description for an error code.
     */
    static const char* getStandardMessage(int16_t code);

private:
    ScpiError m_buffer[SCPI_ERROR_QUEUE_CAPACITY];
    size_t m_head;
    size_t m_tail;
    size_t m_count;
    bool m_overflow;
};

} /* namespace scpi */
#endif /* __cplusplus */

#endif /* __SCPI_ERROR_QUEUE_H */
