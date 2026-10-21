#include "scpi/scpi_parser.h"
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <cstring>

using namespace scpi;

class MockScpiIo : public IScpiIo {
public:
    std::string rx_data;
    std::string tx_data;
    size_t rx_pos = 0;

    void inject(const std::string& str) {
        rx_data += str;
    }

    std::string popTx() {
        std::string ret = tx_data;
        tx_data.clear();
        return ret;
    }

    int write(const uint8_t* data, size_t len) override {
        tx_data.append((const char*)data, len);
        return (int)len;
    }

    int putc(char c) override {
        tx_data.push_back(c);
        return 1;
    }

    int getc() override {
        if (rx_pos < rx_data.size()) {
            return (uint8_t)rx_data[rx_pos++];
        }
        return -1;
    }

    bool hasRx() const override {
        return (rx_pos < rx_data.size());
    }

    void flush() override {}
};

/* Test Fixture Data */
static float s_test_speed = 0.0f;
static int s_test_dir = 1;
static bool s_rst_called = false;

static ScpiStatus handle_speed(ScpiContext& ctx) {
    if (ctx.isQuery()) {
        ctx.replyFloat(s_test_speed, 1);
    } else {
        float val;
        if (ctx.getParamFloat(val, 0.0f, 10000.0f)) {
            s_test_speed = val;
        }
    }
    return SCPI_RES_OK;
}

static ScpiStatus handle_direction(ScpiContext& ctx) {
    if (ctx.isQuery()) {
        ctx.replyString(s_test_dir > 0 ? "CW" : "CCW");
    } else {
        static const char* choices[] = {"CW", "CCW"};
        int idx = 0;
        if (ctx.getParamEnum(idx, choices, 2)) {
            s_test_dir = (idx == 0) ? 1 : -1;
        }
    }
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_volt(ScpiContext& ctx) {
    ctx.replyFloat(16.05f, 2);
    return SCPI_RES_OK;
}

static const ScpiCommandDef s_test_commands[] = {
    { "SOURce:MOTor:SPEEd[:COMManded]", SCPI_CMD_AND_QUERY, &handle_speed },
    { "SOURce:MOTor:DIRection",         SCPI_CMD_AND_QUERY, &handle_direction },
    { "MEASure:VOLTage[:DC]?",          SCPI_QUERY_ONLY,     &handle_meas_volt },
};

static void reset_handler() {
    s_rst_called = true;
    s_test_speed = 0.0f;
    s_test_dir = 1;
}

int main() {
    std::cout << "[TEST] Starting SCPI Parser Host Unit Tests..." << std::endl;

    MockScpiIo io;
    ScpiParser parser(io);
    parser.setIdn("STMicroelectronics,TestFixture,0,v1.0.0");
    parser.registerCommands(s_test_commands, sizeof(s_test_commands) / sizeof(s_test_commands[0]));
    parser.setResetCallback(&reset_handler);

    /* Test 1: *IDN? Query */
    {
        io.inject("*IDN?\n");
        parser.process();
        std::string resp = io.popTx();
        assert(resp == "STMicroelectronics,TestFixture,0,v1.0.0\r\n");
        std::cout << "  [PASS] Test 1: *IDN? Query" << std::endl;
    }

    /* Test 2: *RST Command */
    {
        s_rst_called = false;
        io.inject("*RST\n");
        parser.process();
        assert(s_rst_called == true);
        std::cout << "  [PASS] Test 2: *RST Command" << std::endl;
    }

    /* Test 3: Short form command and query */
    {
        io.inject("SOUR:MOT:SPEE 1450.5\n");
        parser.process();
        assert(s_test_speed == 1450.5f);

        io.inject("SOUR:MOT:SPEE?\n");
        parser.process();
        std::string resp = io.popTx();
        assert(resp == "1450.5\r\n");
        std::cout << "  [PASS] Test 3: Short Form Command & Query" << std::endl;
    }

    /* Test 4: Long form with optional nodes */
    {
        io.inject("SOURCE:MOTOR:SPEED:COMMANDED 2800\n");
        parser.process();
        assert(s_test_speed == 2800.0f);

        io.inject("SOURCE:MOTOR:SPEED:COMMANDED?\n");
        parser.process();
        std::string resp = io.popTx();
        assert(resp == "2800.0\r\n");
        std::cout << "  [PASS] Test 4: Long Form with Optional Nodes" << std::endl;
    }

    /* Test 5: Optional leaf node in query */
    {
        io.inject("MEAS:VOLT?\n");
        parser.process();
        std::string resp1 = io.popTx();
        assert(resp1 == "16.05\r\n");

        io.inject("MEASURE:VOLTAGE:DC?\n");
        parser.process();
        std::string resp2 = io.popTx();
        assert(resp2 == "16.05\r\n");
        std::cout << "  [PASS] Test 5: Optional Leaf Node Query" << std::endl;
    }

    /* Test 6: Enum parameter parsing */
    {
        io.inject("SOUR:MOT:DIR CCW\n");
        parser.process();
        assert(s_test_dir == -1);

        io.inject("SOUR:MOT:DIR?\n");
        parser.process();
        assert(io.popTx() == "CCW\r\n");

        io.inject("SOUR:MOT:DIR CW\n");
        parser.process();
        assert(s_test_dir == 1);
        std::cout << "  [PASS] Test 6: Enum Parameter Parsing" << std::endl;
    }

    /* Test 7: Semicolon Compound Command */
    {
        io.inject("*CLS;SOUR:MOT:SPEE 750;*OPC?\n");
        parser.process();
        assert(s_test_speed == 750.0f);
        std::string resp = io.popTx();
        assert(resp == "1\r\n");
        std::cout << "  [PASS] Test 7: Semicolon Compound Command" << std::endl;
    }

    /* Test 8: Error Queue & :SYST:ERR? */
    {
        io.inject("UNKNOWN:HEADER?\n");
        parser.process();

        io.inject(":SYST:ERR?\n");
        parser.process();
        std::string err_resp = io.popTx();
        assert(err_resp.find("-113,\"Undefined header\"") != std::string::npos);

        /* Empty queue pops 0,"No error" */
        io.inject(":SYST:ERR?\n");
        parser.process();
        std::string empty_resp = io.popTx();
        assert(empty_resp.find("0,\"No error\"") != std::string::npos);
        std::cout << "  [PASS] Test 8: Error Queue & :SYST:ERR?" << std::endl;
    }

    /* Test 9: Data out of range */
    {
        io.inject("SOUR:MOT:SPEE 99999\n"); // Max is 10000.0
        parser.process();

        io.inject(":SYST:ERR?\n");
        parser.process();
        std::string err_resp = io.popTx();
        assert(err_resp.find("-222,\"Data out of range\"") != std::string::npos);
        std::cout << "  [PASS] Test 9: Data Out of Range Error" << std::endl;
    }

    /* Test 10: Status Byte & ESR operations */
    {
        io.inject("*CLS\n");
        parser.process();

        io.inject("*ESR?\n");
        parser.process();
        assert(io.popTx() == "0\r\n");

        io.inject("*OPC\n");
        parser.process();

        io.inject("*ESR?\n");
        parser.process();
        assert(io.popTx() == "1\r\n"); // Bit 0 (OPC) was set

        /* Reading ESR cleared it */
        io.inject("*ESR?\n");
        parser.process();
        assert(io.popTx() == "0\r\n");
        std::cout << "  [PASS] Test 10: Status Byte & ESR Register Operations" << std::endl;
    }

    std::cout << "\n>>> ALL 10 UNIT TESTS PASSED SUCCESSFULLY! <<<\n" << std::endl;
    return 0;
}
