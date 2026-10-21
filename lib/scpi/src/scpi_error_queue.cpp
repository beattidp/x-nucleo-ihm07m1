#include "scpi/scpi_error_queue.h"
#include <string.h>
#include <stdio.h>

namespace scpi {

ScpiErrorQueue::ScpiErrorQueue()
    : m_head(0), m_tail(0), m_count(0), m_overflow(false) {
    memset(m_buffer, 0, sizeof(m_buffer));
}

const char* ScpiErrorQueue::getStandardMessage(int16_t code) {
    switch (code) {
        case SCPI_ERR_NONE:                  return "No error";
        case SCPI_ERR_SYNTAX:                return "Command error";
        case SCPI_ERR_INVALID_SEPARATOR:     return "Invalid separator";
        case SCPI_ERR_DATA_TYPE:             return "Data type error";
        case SCPI_ERR_PARAMETER_NOT_ALLOWED: return "Parameter not allowed";
        case SCPI_ERR_MISSING_PARAMETER:     return "Missing parameter";
        case SCPI_ERR_UNDEFINED_HEADER:      return "Undefined header";
        case SCPI_ERR_EXECUTION_ERROR:       return "Execution error";
        case SCPI_ERR_DATA_OUT_OF_RANGE:     return "Data out of range";
        case SCPI_ERR_HARDWARE_ERROR:        return "Hardware error";
        case SCPI_ERR_QUEUE_OVERFLOW:        return "Queue overflow";
        case SCPI_ERR_QUERY_INTERRUPTED:     return "Query INTERRUPTED";
        case SCPI_ERR_QUERY_UNTERMINATED:    return "Query UNTERMINATED";
        default:                             return "Unknown error";
    }
}

void ScpiErrorQueue::push(int16_t code, const char* custom_msg) {
    if (code == SCPI_ERR_NONE) {
        return;
    }

    const char* msg = custom_msg ? custom_msg : getStandardMessage(code);

    if (m_count >= SCPI_ERROR_QUEUE_CAPACITY) {
        /* Queue overflow: According to SCPI standard, the last entry is replaced with -350 */
        m_overflow = true;
        size_t last_idx = (m_head == 0) ? (SCPI_ERROR_QUEUE_CAPACITY - 1) : (m_head - 1);
        m_buffer[last_idx].code = SCPI_ERR_QUEUE_OVERFLOW;
        strncpy(m_buffer[last_idx].message, getStandardMessage(SCPI_ERR_QUEUE_OVERFLOW), SCPI_ERROR_MSG_MAX_LEN - 1);
        m_buffer[last_idx].message[SCPI_ERROR_MSG_MAX_LEN - 1] = '\0';
        return;
    }

    m_buffer[m_head].code = code;
    strncpy(m_buffer[m_head].message, msg, SCPI_ERROR_MSG_MAX_LEN - 1);
    m_buffer[m_head].message[SCPI_ERROR_MSG_MAX_LEN - 1] = '\0';

    m_head = (m_head + 1) % SCPI_ERROR_QUEUE_CAPACITY;
    m_count++;
}

bool ScpiErrorQueue::pop(ScpiError& out) {
    if (m_count == 0) {
        out.code = SCPI_ERR_NONE;
        strncpy(out.message, getStandardMessage(SCPI_ERR_NONE), SCPI_ERROR_MSG_MAX_LEN - 1);
        out.message[SCPI_ERROR_MSG_MAX_LEN - 1] = '\0';
        return false;
    }

    out = m_buffer[m_tail];
    m_tail = (m_tail + 1) % SCPI_ERROR_QUEUE_CAPACITY;
    m_count--;

    if (m_count == 0) {
        m_overflow = false;
    }

    return true;
}

void ScpiErrorQueue::clear() {
    m_head = 0;
    m_tail = 0;
    m_count = 0;
    m_overflow = false;
    memset(m_buffer, 0, sizeof(m_buffer));
}

size_t ScpiErrorQueue::count() const {
    return m_count;
}

bool ScpiErrorQueue::isEmpty() const {
    return (m_count == 0);
}

} /* namespace scpi */
