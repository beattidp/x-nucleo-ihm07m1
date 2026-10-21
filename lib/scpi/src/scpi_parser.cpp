#include "scpi/scpi_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

namespace scpi {

/* ============================================================================
 * ScpiContext Implementation
 * ============================================================================ */

ScpiContext::ScpiContext(ScpiParser& parser, const char* header, const char* params, bool is_query)
    : m_parser(parser), m_header(header), m_params(params), m_is_query(is_query) {
}

bool ScpiContext::hasParams() const {
    if (!m_params) return false;
    const char* p = m_params;
    while (*p && isspace((unsigned char)*p)) p++;
    return (*p != '\0');
}

bool ScpiContext::getParamFloat(float& out, float min_val, float max_val) {
    if (!hasParams()) {
        reportError(SCPI_ERR_MISSING_PARAMETER);
        return false;
    }

    const char* p = m_params;
    while (*p && isspace((unsigned char)*p)) p++;

    char* endptr = nullptr;
    float val = strtof(p, &endptr);
    if (endptr == p) {
        reportError(SCPI_ERR_DATA_TYPE);
        return false;
    }

    if (val < min_val || val > max_val) {
        reportError(SCPI_ERR_DATA_OUT_OF_RANGE);
        return false;
    }

    out = val;
    return true;
}

bool ScpiContext::getParamInt(int32_t& out, int32_t min_val, int32_t max_val) {
    if (!hasParams()) {
        reportError(SCPI_ERR_MISSING_PARAMETER);
        return false;
    }

    const char* p = m_params;
    while (*p && isspace((unsigned char)*p)) p++;

    char* endptr = nullptr;
    long val = strtol(p, &endptr, 0);
    if (endptr == p) {
        reportError(SCPI_ERR_DATA_TYPE);
        return false;
    }

    if (val < (long)min_val || val > (long)max_val) {
        reportError(SCPI_ERR_DATA_OUT_OF_RANGE);
        return false;
    }

    out = (int32_t)val;
    return true;
}

static bool str_equals_nocase(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        if (toupper((unsigned char)*s1) != toupper((unsigned char)*s2)) return false;
        s1++;
        s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

bool ScpiContext::getParamBool(bool& out) {
    if (!hasParams()) {
        reportError(SCPI_ERR_MISSING_PARAMETER);
        return false;
    }

    const char* p = m_params;
    while (*p && isspace((unsigned char)*p)) p++;

    char token[16];
    size_t i = 0;
    while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != ';' && i < sizeof(token) - 1) {
        token[i++] = *p++;
    }
    token[i] = '\0';

    if (str_equals_nocase(token, "1") || str_equals_nocase(token, "ON")) {
        out = true;
        return true;
    } else if (str_equals_nocase(token, "0") || str_equals_nocase(token, "OFF")) {
        out = false;
        return true;
    }

    reportError(SCPI_ERR_DATA_TYPE);
    return false;
}

bool ScpiContext::getParamEnum(int& out_idx, const char* const* choices, size_t choice_count) {
    if (!hasParams()) {
        reportError(SCPI_ERR_MISSING_PARAMETER);
        return false;
    }

    const char* p = m_params;
    while (*p && isspace((unsigned char)*p)) p++;

    char token[32];
    size_t i = 0;
    while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != ';' && i < sizeof(token) - 1) {
        token[i++] = *p++;
    }
    token[i] = '\0';

    for (size_t c = 0; c < choice_count; c++) {
        if (str_equals_nocase(token, choices[c])) {
            out_idx = (int)c;
            return true;
        }
    }

    reportError(SCPI_ERR_DATA_TYPE);
    return false;
}

bool ScpiContext::getParamString(char* out, size_t max_len) {
    if (!hasParams() || max_len == 0) {
        reportError(SCPI_ERR_MISSING_PARAMETER);
        return false;
    }

    const char* p = m_params;
    while (*p && isspace((unsigned char)*p)) p++;

    char quote = '\0';
    if (*p == '"' || *p == '\'') {
        quote = *p++;
    }

    size_t idx = 0;
    if (quote) {
        while (*p && *p != quote && idx < max_len - 1) {
            out[idx++] = *p++;
        }
    } else {
        while (*p && !isspace((unsigned char)*p) && *p != ';' && idx < max_len - 1) {
            out[idx++] = *p++;
        }
    }
    out[idx] = '\0';
    return true;
}

void ScpiContext::replyRaw(const char* text) {
    if (!text) return;
    m_parser.appendResponse(text, strlen(text));
}

void ScpiContext::replyString(const char* str) {
    replyRaw(str);
}

void ScpiContext::replyFloat(float val, int decimals) {
    char buf[32];
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    snprintf(buf, sizeof(buf), fmt, val);
    replyRaw(buf);
}

void ScpiContext::replyInt(int32_t val) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%ld", (long)val);
    replyRaw(buf);
}

void ScpiContext::replyBool(bool val) {
    replyRaw(val ? "1" : "0");
}

void ScpiContext::replyPrintf(const char* fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    replyRaw(buf);
}

void ScpiContext::reportError(int16_t code, const char* custom_msg) {
    m_parser.pushError(code, custom_msg);
}

/* ============================================================================
 * ScpiParser Implementation
 * ============================================================================ */

ScpiParser::ScpiParser(IScpiIo& io)
    : m_io(io),
      m_commands(nullptr),
      m_command_count(0),
      m_reset_cb(nullptr),
      m_selftest_cb(nullptr),
      m_stb(0),
      m_sre(0),
      m_esr(0),
      m_ese(0),
      m_rx_idx(0),
      m_tx_idx(0),
      m_response_pending(false) {
    strncpy(m_idn, "Antigravity,SCPI-Device,0,v1.0.0", sizeof(m_idn) - 1);
    m_idn[sizeof(m_idn) - 1] = '\0';
    memset(m_rx_buf, 0, sizeof(m_rx_buf));
    memset(m_tx_buf, 0, sizeof(m_tx_buf));
}

void ScpiParser::setIdn(const char* idn_str) {
    if (idn_str) {
        strncpy(m_idn, idn_str, sizeof(m_idn) - 1);
        m_idn[sizeof(m_idn) - 1] = '\0';
    }
}

void ScpiParser::registerCommands(const ScpiCommandDef* table, size_t count) {
    m_commands = table;
    m_command_count = count;
}

void ScpiParser::pushError(int16_t code, const char* msg) {
    m_error_queue.push(code, msg);
    /* Update ESR error bits according to SCPI error category */
    if (code <= -100 && code >= -199) {
        setEventStatusBit(SCPI_ESR_COMMAND_ERROR);
    } else if (code <= -200 && code >= -299) {
        setEventStatusBit(SCPI_ESR_EXECUTION_ERROR);
    } else if (code <= -300 && code >= -399) {
        setEventStatusBit(SCPI_ESR_DEVICE_DEPENDENT_ERROR);
    } else if (code <= -400 && code >= -499) {
        setEventStatusBit(SCPI_ESR_QUERY_ERROR);
    }
}

uint8_t ScpiParser::getStatusByte() {
    uint8_t stb = m_stb & ~(SCPI_STB_ERROR_QUEUE_NOT_EMPTY | SCPI_STB_EVENT_STATUS_SUMMARY | SCPI_STB_MASTER_SUMMARY_STATUS);

    if (!m_error_queue.isEmpty()) {
        stb |= SCPI_STB_ERROR_QUEUE_NOT_EMPTY;
    }
    if ((m_esr & m_ese) != 0) {
        stb |= SCPI_STB_EVENT_STATUS_SUMMARY;
    }
    if ((stb & m_sre) != 0) {
        stb |= SCPI_STB_MASTER_SUMMARY_STATUS;
    }

    m_stb = stb;
    return stb;
}

uint8_t ScpiParser::getEventStatus() {
    uint8_t val = m_esr;
    m_esr = 0; /* Reading ESR clears it per IEEE 488.2 */
    return val;
}

void ScpiParser::setEventStatusBit(uint8_t bitmask) {
    m_esr |= bitmask;
}

void ScpiParser::clearEventStatus() {
    m_esr = 0;
}

void ScpiParser::appendResponse(const char* str, size_t len) {
    if (!str || len == 0) return;
    for (size_t i = 0; i < len; i++) {
        if (m_tx_idx < SCPI_TX_BUFFER_SIZE - 3) {
            m_tx_buf[m_tx_idx++] = str[i];
        }
    }
    m_response_pending = true;
}

void ScpiParser::flushResponse() {
    if (m_response_pending) {
        /* Append standard CRLF terminator */
        if (m_tx_idx < SCPI_TX_BUFFER_SIZE - 2) {
            m_tx_buf[m_tx_idx++] = '\r';
            m_tx_buf[m_tx_idx++] = '\n';
        }
        m_tx_buf[m_tx_idx] = '\0';
        m_io.write((const uint8_t*)m_tx_buf, m_tx_idx);
        m_tx_idx = 0;
        m_response_pending = false;
        m_io.flush();
    }
}

void ScpiParser::process() {
    while (m_io.hasRx()) {
        int ch = m_io.getc();
        if (ch == -1) break;

        char c = (char)ch;
        if (c == '\n' || c == '\r') {
            if (m_rx_idx > 0) {
                m_rx_buf[m_rx_idx] = '\0';
                executeLine(m_rx_buf);
                m_rx_idx = 0;
            }
        } else {
            if (m_rx_idx < SCPI_RX_BUFFER_SIZE - 1) {
                m_rx_buf[m_rx_idx++] = c;
            } else {
                /* Buffer overrun: discard line and flag command error */
                m_rx_idx = 0;
                pushError(SCPI_ERR_SYNTAX, "Line buffer overrun");
            }
        }
    }
}

void ScpiParser::executeLine(const char* line) {
    if (!line) return;

    /* Make local mutable copy for tokenization */
    char line_copy[SCPI_RX_BUFFER_SIZE];
    strncpy(line_copy, line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';

    /* Semicolon ';' splits line into subcommands */
    char* cur = line_copy;
    while (*cur) {
        /* Skip leading whitespace */
        while (*cur && isspace((unsigned char)*cur)) cur++;
        if (!*cur) break;

        char* sc_start = cur;
        /* Find next semicolon, taking care of quoted strings */
        bool in_quote = false;
        char quote_char = '\0';
        while (*cur) {
            if ((*cur == '"' || *cur == '\'') && (!in_quote || *cur == quote_char)) {
                in_quote = !in_quote;
                quote_char = in_quote ? *cur : '\0';
            } else if (*cur == ';' && !in_quote) {
                break;
            }
            cur++;
        }

        if (*cur == ';') {
            *cur = '\0';
            cur++;
        }

        /* Execute individual subcommand */
        executeSubcommand(sc_start);
    }

    flushResponse();
}

void ScpiParser::executeSubcommand(char* cmd_str) {
    /* Trim trailing whitespace */
    size_t len = strlen(cmd_str);
    while (len > 0 && isspace((unsigned char)cmd_str[len - 1])) {
        cmd_str[--len] = '\0';
    }
    if (len == 0) return;

    /* Separate header from parameters at first whitespace */
    char* header = cmd_str;
    char* params = nullptr;

    char* ws = header;
    while (*ws && !isspace((unsigned char)*ws)) ws++;
    if (*ws) {
        *ws = '\0';
        params = ws + 1;
        while (*params && isspace((unsigned char)*params)) params++;
    }

    /* Check if header is a query */
    size_t hdr_len = strlen(header);
    bool is_query = false;
    if (hdr_len > 0 && header[hdr_len - 1] == '?') {
        is_query = true;
    }

    /* 1. IEEE 488.2 Common Commands (*IDN?, *RST, etc.) */
    if (header[0] == '*') {
        handleCommonCommand(header, params, is_query);
        return;
    }

    /* 2. System Subtree (:SYST:ERR?, :SYST:VERS?, etc.) */
    const char* norm_hdr = (header[0] == ':') ? (header + 1) : header;
    if (strncasecmp(norm_hdr, "SYST", 4) == 0) {
        handleSystemSubtree(header, params, is_query);
        return;
    }

    /* 3. Device-Specific Command Table */
    bool matched = false;
    if (m_commands) {
        for (size_t i = 0; i < m_command_count; i++) {
            const ScpiCommandDef& def = m_commands[i];
            if (matchPattern(def.pattern, header, is_query)) {
                matched = true;

                /* Check query validity */
                if (is_query && !(def.flags & SCPI_QUERY_ONLY)) {
                    pushError(SCPI_ERR_UNDEFINED_HEADER, "Command is not a query");
                    return;
                }
                if (!is_query && !(def.flags & SCPI_CMD_ONLY)) {
                    pushError(SCPI_ERR_UNDEFINED_HEADER, "Query requires '?'");
                    return;
                }

                /* Execute handler */
                ScpiContext ctx(*this, header, params, is_query);
                if (def.handler) {
                    def.handler(ctx);
                }
                break;
            }
        }
    }

    if (!matched) {
        pushError(SCPI_ERR_UNDEFINED_HEADER);
    }
}

/* ============================================================================
 * Pattern & Node Matching (Short/Long Form & Hierarchy)
 * ============================================================================ */

bool ScpiParser::matchNode(const char* pat_node, size_t pat_len, const char* hdr_node, size_t hdr_len) {
    if (pat_len == 0 || hdr_len == 0) return false;

    /* Count uppercase mandatory letters in pattern node */
    size_t mand_len = 0;
    while (mand_len < pat_len && isupper((unsigned char)pat_node[mand_len])) {
        mand_len++;
    }

    /* If no uppercase, treat whole node as mandatory */
    if (mand_len == 0) {
        mand_len = pat_len;
    }

    /* Match Short Form */
    if (hdr_len == mand_len) {
        for (size_t i = 0; i < mand_len; i++) {
            if (toupper((unsigned char)hdr_node[i]) != toupper((unsigned char)pat_node[i])) {
                return false;
            }
        }
        return true;
    }

    /* Match Long Form */
    if (hdr_len == pat_len) {
        for (size_t i = 0; i < pat_len; i++) {
            if (toupper((unsigned char)hdr_node[i]) != toupper((unsigned char)pat_node[i])) {
                return false;
            }
        }
        return true;
    }

    return false;
}

bool ScpiParser::matchPattern(const char* pattern, const char* header, bool is_query) {
    if (!pattern || !header) return false;

    (void)is_query;

    struct PatNode {
        const char* start;
        size_t len;
        bool is_optional;
    };

    struct HdrNode {
        const char* start;
        size_t len;
    };

    PatNode p_nodes[8];
    size_t p_count = 0;

    const char* p = pattern;
    while (*p == ':') p++;

    while (*p && p_count < 8) {
        bool opt = false;
        if (*p == '[') {
            opt = true;
            p++;
            if (*p == ':') p++;
        }
        const char* n_start = p;
        while (*p && *p != ':' && *p != '[' && *p != ']' && *p != '?') p++;
        size_t n_len = p - n_start;
        if (n_len > 0) {
            p_nodes[p_count++] = { n_start, n_len, opt };
        }
        if (*p == ']') p++;
        if (*p == ':') p++;
        if (*p == '?') p++;
    }

    HdrNode h_nodes[8];
    size_t h_count = 0;

    const char* h = header;
    while (*h == ':') h++;

    while (*h && h_count < 8) {
        const char* n_start = h;
        while (*h && *h != ':' && *h != '?') h++;
        size_t n_len = h - n_start;
        if (n_len > 0) {
            h_nodes[h_count++] = { n_start, n_len };
        }
        if (*h == ':') h++;
        if (*h == '?') break;
    }

    size_t p_idx = 0;
    size_t h_idx = 0;

    while (p_idx < p_count && h_idx < h_count) {
        if (matchNode(p_nodes[p_idx].start, p_nodes[p_idx].len,
                      h_nodes[h_idx].start, h_nodes[h_idx].len)) {
            p_idx++;
            h_idx++;
        } else if (p_nodes[p_idx].is_optional) {
            p_idx++;
        } else {
            return false;
        }
    }

    /* Skip any remaining optional pattern nodes */
    while (p_idx < p_count && p_nodes[p_idx].is_optional) {
        p_idx++;
    }

    return (p_idx == p_count && h_idx == h_count);
}

/* ============================================================================
 * Built-in IEEE 488.2 Common Commands
 * ============================================================================ */

void ScpiParser::handleCommonCommand(const char* header, const char* params, bool is_query) {
    ScpiContext ctx(*this, header, params, is_query);

    if (strcasecmp(header, "*IDN?") == 0) {
        ctx.replyString(m_idn);
    } else if (strcasecmp(header, "*RST") == 0) {
        if (m_reset_cb) m_reset_cb();
    } else if (strcasecmp(header, "*CLS") == 0) {
        m_error_queue.clear();
        clearEventStatus();
    } else if (strcasecmp(header, "*OPC") == 0) {
        setEventStatusBit(SCPI_ESR_OPERATION_COMPLETE);
    } else if (strcasecmp(header, "*OPC?") == 0) {
        ctx.replyString("1");
    } else if (strcasecmp(header, "*STB?") == 0) {
        ctx.replyInt(getStatusByte());
    } else if (strcasecmp(header, "*ESR?") == 0) {
        ctx.replyInt(getEventStatus());
    } else if (strcasecmp(header, "*ESE") == 0) {
        int32_t val = 0;
        if (ctx.getParamInt(val, 0, 255)) {
            setEventStatusEnable((uint8_t)val);
        }
    } else if (strcasecmp(header, "*ESE?") == 0) {
        ctx.replyInt(getEventStatusEnable());
    } else if (strcasecmp(header, "*SRE") == 0) {
        int32_t val = 0;
        if (ctx.getParamInt(val, 0, 255)) {
            setServiceRequestEnable((uint8_t)val);
        }
    } else if (strcasecmp(header, "*SRE?") == 0) {
        ctx.replyInt(getServiceRequestEnable());
    } else if (strcasecmp(header, "*TST?") == 0) {
        int res = m_selftest_cb ? m_selftest_cb() : 0;
        ctx.replyInt(res);
    } else if (strcasecmp(header, "*WAI") == 0) {
        /* No-op on synchronous embedded target */
    } else {
        pushError(SCPI_ERR_UNDEFINED_HEADER);
    }
}

/* ============================================================================
 * Built-in System Subsystem
 * ============================================================================ */

void ScpiParser::handleSystemSubtree(const char* header, const char* params, bool is_query) {
    ScpiContext ctx(*this, header, params, is_query);

    /* Match :SYSTem:ERRor[:NEXT]? */
    if (matchPattern("SYSTem:ERRor[:NEXT]?", header, is_query)) {
        ScpiError err;
        m_error_queue.pop(err);
        ctx.replyPrintf("%d,\"%s\"", err.code, err.message);
    } else if (matchPattern("SYSTem:ERRor:COUNt?", header, is_query)) {
        ctx.replyInt((int32_t)m_error_queue.count());
    } else if (matchPattern("SYSTem:VERSion?", header, is_query)) {
        ctx.replyString("1999.0");
    } else {
        pushError(SCPI_ERR_UNDEFINED_HEADER);
    }
}

} /* namespace scpi */
