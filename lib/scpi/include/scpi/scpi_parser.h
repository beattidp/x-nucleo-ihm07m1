#ifndef __SCPI_PARSER_H
#define __SCPI_PARSER_H

#include "scpi_types.h"
#include "scpi_io.h"
#include "scpi_error_queue.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
namespace scpi {

#define SCPI_RX_BUFFER_SIZE         256
#define SCPI_TX_BUFFER_SIZE         256
#define SCPI_IDN_STRING_MAX_LEN     80

class ScpiParser;
class ScpiContext;

/**
 * @brief Command handler function pointer
 */
typedef ScpiStatus (*ScpiHandler)(ScpiContext& ctx);

/**
 * @brief Command table entry definition
 */
struct ScpiCommandDef {
    const char* pattern;       /* e.g. "SOURce:MOTor:SPEEd[:COMManded]" */
    uint8_t flags;             /* SCPI_CMD_ONLY, SCPI_QUERY_ONLY, SCPI_CMD_AND_QUERY */
    ScpiHandler handler;       /* Execution callback */
};

/**
 * @brief Command Execution Context
 *
 * Passed to handlers to extract arguments, check query state, format responses,
 * and report command errors.
 */
class ScpiContext {
public:
    ScpiContext(ScpiParser& parser, const char* header, const char* params, bool is_query);

    ScpiParser& parser() { return m_parser; }
    const char* header() const { return m_header; }
    const char* params() const { return m_params; }
    bool isQuery() const { return m_is_query; }
    bool hasParams() const;

    /* Parameter Extraction Helpers */
    bool getParamFloat(float& out, float min_val = -1e9f, float max_val = 1e9f);
    bool getParamInt(int32_t& out, int32_t min_val = -2147483647, int32_t max_val = 2147483647);
    bool getParamBool(bool& out);
    bool getParamEnum(int& out_idx, const char* const* choices, size_t choice_count);
    bool getParamString(char* out, size_t max_len);

    /* Response Helpers (Appends to response buffer) */
    void replyRaw(const char* text);
    void replyString(const char* str);
    void replyFloat(float val, int decimals = 2);
    void replyInt(int32_t val);
    void replyBool(bool val);
    void replyPrintf(const char* fmt, ...);

    /* Error Emission */
    void reportError(int16_t code, const char* custom_msg = nullptr);

private:
    ScpiParser& m_parser;
    const char* m_header;
    const char* m_params;
    bool m_is_query;
};

/**
 * @brief Core Zero-Heap Reusable Embedded SCPI Parser Engine
 */
class ScpiParser {
public:
    explicit ScpiParser(IScpiIo& io);

    /**
     * @brief Configures IEEE 488.2 *IDN? response string
     * e.g. "STMicroelectronics,X-NUCLEO-IHM07M1-STM32G474RE,0,v1.0.0"
     */
    void setIdn(const char* idn_str);

    /**
     * @brief Registers the device-specific command table
     */
    void registerCommands(const ScpiCommandDef* table, size_t count);

    /**
     * @brief Optional reset callback triggered by *RST
     */
    typedef void (*ResetCallback)(void);
    void setResetCallback(ResetCallback cb) { m_reset_cb = cb; }

    /**
     * @brief Optional self-test callback triggered by *TST?
     * Returns 0 on pass, or error code on fail.
     */
    typedef int (*SelfTestCallback)(void);
    void setSelfTestCallback(SelfTestCallback cb) { m_selftest_cb = cb; }

    /**
     * @brief Polls receive transport and executes completed lines.
     * Call periodically in superloop.
     */
    void process();

    /**
     * @brief Directly feeds and executes a raw SCPI command line string.
     * Useful for testing and loopback injection.
     */
    void executeLine(const char* line);

    /**
     * @brief Error Queue access
     */
    ScpiErrorQueue& errorQueue() { return m_error_queue; }
    void pushError(int16_t code, const char* msg = nullptr);

    /**
     * @brief IEEE 488.2 Status Registers
     */
    uint8_t getStatusByte();
    uint8_t getEventStatus();
    void setEventStatusBit(uint8_t bitmask);
    void clearEventStatus();
    void setEventStatusEnable(uint8_t mask) { m_ese = mask; }
    uint8_t getEventStatusEnable() const { return m_ese; }
    void setServiceRequestEnable(uint8_t mask) { m_sre = mask; }
    uint8_t getServiceRequestEnable() const { return m_sre; }

    /* Output buffer access used by ScpiContext */
    void appendResponse(const char* str, size_t len);
    void flushResponse();

private:
    IScpiIo& m_io;
    const ScpiCommandDef* m_commands;
    size_t m_command_count;
    ScpiErrorQueue m_error_queue;

    char m_idn[SCPI_IDN_STRING_MAX_LEN];
    ResetCallback m_reset_cb;
    SelfTestCallback m_selftest_cb;

    /* Status Registers */
    uint8_t m_stb;
    uint8_t m_sre;
    uint8_t m_esr;
    uint8_t m_ese;

    /* Buffers */
    char m_rx_buf[SCPI_RX_BUFFER_SIZE];
    size_t m_rx_idx;

    char m_tx_buf[SCPI_TX_BUFFER_SIZE];
    size_t m_tx_idx;
    bool m_response_pending;

    /* Execution & Tokenizer internals */
    void executeSubcommand(char* cmd_str);
    bool matchPattern(const char* pattern, const char* header, bool is_query);
    bool matchNode(const char* pat_node, size_t pat_len, const char* hdr_node, size_t hdr_len);
    void handleCommonCommand(const char* header, const char* params, bool is_query);
    bool handleSystemSubtree(const char* header, const char* params, bool is_query);
};

} /* namespace scpi */
#endif /* __cplusplus */

#endif /* __SCPI_PARSER_H */
