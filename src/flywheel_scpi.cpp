#include "flywheel_scpi.h"
#include "scpi/scpi_parser.h"
#include "motor_control.h"
#include "motor_profiles.h"
#include "motor_characterization.h"
#include "power_stage.h"
#include <stdio.h>
#include <string.h>

using namespace scpi;

/**
 * @brief LPUART1 Stream Transport Implementation
 */
class LpuartScpiIo : public IScpiIo {
public:
    int write(const uint8_t* data, size_t len) override {
        return console_write(data, (uint16_t)len);
    }

    int putc(char c) override {
        return console_putc(c);
    }

    int getc() override {
        return console_getch();
    }

    bool hasRx() const override {
        return console_has_char();
    }

    void flush() override {
        /* Circular buffer interrupt transmitter auto-flushes */
    }
};

static LpuartScpiIo s_io;
static ScpiParser s_parser(s_io);

/* ============================================================================
 * SCPI Command Handlers
 * ============================================================================ */

/* --- System Communication Mode --- */
static ScpiStatus handle_comm_mode(ScpiContext& ctx) {
    if (ctx.isQuery()) {
        ctx.replyString(console_get_mode() == COMM_MODE_SCPI ? "SCPI" : "CONSOLE");
    } else {
        static const char* choices[] = {"CONSOLE", "SCPI"};
        int idx = 0;
        if (ctx.getParamEnum(idx, choices, 2)) {
            console_set_mode(idx == 0 ? COMM_MODE_CONSOLE : COMM_MODE_SCPI);
        }
    }
    return SCPI_RES_OK;
}

/* --- Motor Output State (:OUTPut[:STATe]) --- */
static ScpiStatus handle_output_state(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    if (ctx.isQuery()) {
        bool running = (ctrl->state == MOTOR_STATE_RUNNING || ctrl->state == MOTOR_STATE_STARTING);
        ctx.replyBool(running);
    } else {
        bool on = false;
        if (ctx.getParamBool(on)) {
            if (on) {
                Motor_Start();
            } else {
                Motor_Stop();
            }
        }
    }
    return SCPI_RES_OK;
}

/* --- Motor Brake (:OUTPut:BRAKe) --- */
static ScpiStatus handle_output_brake(ScpiContext& ctx) {
    (void)ctx;
    Motor_Brake();
    return SCPI_RES_OK;
}

/* --- Commanded Speed (:SOURce:MOTor:SPEEd[:COMManded]) --- */
static ScpiStatus handle_motor_speed(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    if (ctx.isQuery()) {
        ctx.replyFloat(ctrl->target_speed_rpm, 1);
    } else {
        float rpm = 0.0f;
        if (ctx.getParamFloat(rpm, 0.0f, 10000.0f)) {
            Motor_SetPotControl(false);
            Motor_SetSpeed(rpm);
        }
    }
    return SCPI_RES_OK;
}

/* --- Rotation Direction (:SOURce:MOTor:DIRection) --- */
static ScpiStatus handle_motor_direction(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    if (ctx.isQuery()) {
        ctx.replyString(ctrl->direction > 0 ? "CW" : "CCW");
    } else {
        static const char* choices[] = {"CW", "CCW", "FORWARD", "REVERSE"};
        int idx = 0;
        if (ctx.getParamEnum(idx, choices, 4)) {
            int8_t dir = (idx == 0 || idx == 2) ? 1 : -1;
            Motor_SetDirection(dir);
        }
    }
    return SCPI_RES_OK;
}

/* --- Commutation Mode (:SOURce:MOTor:MODE) --- */
static ScpiStatus handle_motor_mode(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    if (ctx.isQuery()) {
        switch (ctrl->mode) {
            case MOTOR_MODE_SIX_STEP_VOLTAGE: ctx.replyString("SIXSTEP"); break;
            case MOTOR_MODE_FOC:              ctx.replyString("FOC"); break;
            case MOTOR_MODE_OPEN_LOOP_CHAR:   ctx.replyString("OPENLOOP"); break;
            default:                          ctx.replyString("IDLE"); break;
        }
    } else {
        static const char* choices[] = {"SIXSTEP", "FOC", "OPENLOOP"};
        int idx = 0;
        if (ctx.getParamEnum(idx, choices, 3)) {
            MotorMode_t m = MOTOR_MODE_SIX_STEP_VOLTAGE;
            if (idx == 1) m = MOTOR_MODE_FOC;
            else if (idx == 2) m = MOTOR_MODE_OPEN_LOOP_CHAR;
            Motor_SetMode(m);
        }
    }
    return SCPI_RES_OK;
}

/* --- Manual Duty Cycle (:SOURce:MOTor:DUTY) --- */
static ScpiStatus handle_motor_duty(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    if (ctx.isQuery()) {
        ctx.replyFloat(ctrl->duty_pct, 1);
    } else {
        float val = 0.0f;
        if (ctx.getParamFloat(val, 0.0f, 100.0f)) {
            Motor_SetDuty(val);
        }
    }
    return SCPI_RES_OK;
}

/* --- Slew Rates (:SOURce:MOTor:SLEW:ACCEL and :DECEL) --- */
static ScpiStatus handle_slew_accel(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    if (ctx.isQuery()) {
        ctx.replyFloat(ctrl->accel_slew_rpm_s, 1);
    } else {
        float val = 0.0f;
        if (ctx.getParamFloat(val, 10.0f, 10000.0f)) {
            Motor_SetSlewRates(val, ctrl->decel_slew_rpm_s);
        }
    }
    return SCPI_RES_OK;
}

static ScpiStatus handle_slew_decel(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    if (ctx.isQuery()) {
        ctx.replyFloat(ctrl->decel_slew_rpm_s, 1);
    } else {
        float val = 0.0f;
        if (ctx.getParamFloat(val, 10.0f, 10000.0f)) {
            Motor_SetSlewRates(ctrl->accel_slew_rpm_s, val);
        }
    }
    return SCPI_RES_OK;
}

static ScpiStatus handle_slew_query_both(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    ctx.replyPrintf("%.1f,%.1f", ctrl->accel_slew_rpm_s, ctrl->decel_slew_rpm_s);
    return SCPI_RES_OK;
}

/* --- Current Limit Ceiling (:SOURce:MOTor:CURRent:LIMit) --- */
static ScpiStatus handle_current_limit(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    if (ctx.isQuery()) {
        ctx.replyFloat(ctrl->torque_limit_amps, 2);
    } else {
        float val = 0.0f;
        if (ctx.getParamFloat(val, 0.1f, 5.0f)) {
            Motor_SetTorqueLimit(val);
        }
    }
    return SCPI_RES_OK;
}

/* --- Potentiometer Speed Control (:SOURce:MOTor:POTentiometer[:STATe]) --- */
static ScpiStatus handle_pot_control(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    if (ctx.isQuery()) {
        ctx.replyBool(ctrl->pot_control_enabled);
    } else {
        bool on = false;
        if (ctx.getParamBool(on)) {
            Motor_SetPotControl(on);
        }
    }
    return SCPI_RES_OK;
}

/* --- Motor Profile Selection (:SOURce:MOTor:PROFile:SELect) --- */
static ScpiStatus handle_profile_select(ScpiContext& ctx) {
    if (ctx.isQuery()) {
        ctx.replyInt(MotorProfiles_GetActiveIndex() + 1);
    } else {
        int32_t idx = 1;
        if (ctx.getParamInt(idx, 1, 4)) {
            MotorProfiles_Select((uint8_t)(idx - 1));
        }
    }
    return SCPI_RES_OK;
}

static ScpiStatus handle_profile_name(ScpiContext& ctx) {
    const MotorProfile_t* p = MotorProfiles_GetActive();
    ctx.replyString(p ? p->name : "Unknown");
    return SCPI_RES_OK;
}

static ScpiStatus handle_profile_catalog(ScpiContext& ctx) {
    char buf[128];
    buf[0] = '\0';
    uint8_t count = MotorProfiles_GetCount();
    for (uint8_t i = 0; i < count; i++) {
        const MotorProfile_t* p = MotorProfiles_Get(i);
        if (p) {
            strncat(buf, p->name, sizeof(buf) - strlen(buf) - 1);
            if (i < count - 1) {
                strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
            }
        }
    }
    ctx.replyString(buf);
    return SCPI_RES_OK;
}

/* --- Measurements Subsystem (:MEASure:*) --- */
static ScpiStatus handle_meas_vbus(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyFloat(telem.vbus_v, 2);
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_temp(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyFloat(telem.temp_c, 1);
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_optical_rpm(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyFloat(telem.optical_rpm, 1);
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_hall_rpm(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyFloat(telem.hall_rpm, 1);
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_actual_rpm(ScpiContext& ctx) {
    const MotorControl_t* ctrl = Motor_GetControl();
    ctx.replyFloat(ctrl->actual_speed_rpm, 1);
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_current_peak(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyFloat(telem.current_peak_amps, 2);
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_phase_currents(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyPrintf("%.3f,%.3f,%.3f", telem.current_a_amps, telem.current_b_amps, telem.current_c_amps);
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_current_dq(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyPrintf("%.3f,%.3f", telem.id_amps, telem.iq_amps);
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_optical_pulses(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyInt((int32_t)telem.optical_pulse_count);
    return SCPI_RES_OK;
}

static ScpiStatus handle_meas_telemetry_all(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    const MotorControl_t* ctrl = Motor_GetControl();

    /* High-efficiency single-line synchronized CSV stream for PyVISA */
    ctx.replyPrintf("%.2f,%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.1f,%d,%d,%lu",
                    telem.vbus_v,
                    telem.temp_c,
                    ctrl->actual_speed_rpm,
                    telem.optical_rpm,
                    telem.hall_rpm,
                    telem.iq_amps,
                    telem.id_amps,
                    telem.current_peak_amps,
                    ctrl->duty_pct,
                    telem.motor_stalled ? 1 : 0,
                    telem.fault_active ? 1 : 0,
                    (unsigned long)telem.optical_pulse_count);
    return SCPI_RES_OK;
}

/* --- Diagnostics & Characterization Subsystem (:TEST:* and :FAULt:*) --- */
static ScpiStatus handle_test_stall(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyBool(telem.motor_stalled);
    return SCPI_RES_OK;
}

static ScpiStatus handle_test_slip(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyBool(telem.motor_slipping);
    return SCPI_RES_OK;
}

static ScpiStatus handle_test_reset_optical(ScpiContext& ctx) {
    (void)ctx;
    Motor_ResetOpticalCount();
    return SCPI_RES_OK;
}

static ScpiStatus handle_test_calibrate_current(ScpiContext& ctx) {
    (void)ctx;
    Motor_CalibrateCurrentOffsets();
    return SCPI_RES_OK;
}

static ScpiStatus handle_test_char_start(ScpiContext& ctx) {
    (void)ctx;
    Motor_StartCharacterization();
    return SCPI_RES_OK;
}

static ScpiStatus handle_test_char_abort(ScpiContext& ctx) {
    (void)ctx;
    MotorChar_Abort("SCPI Abort");
    return SCPI_RES_OK;
}

static ScpiStatus handle_test_char_state(ScpiContext& ctx) {
    CharState_t state = MotorChar_GetState();
    uint8_t pct = MotorChar_GetProgressPct();
    const char* state_name = "IDLE";
    switch (state) {
        case CHAR_STATE_CALIBRATING_OFFSETS: state_name = "CALIBRATING"; break;
        case CHAR_STATE_MEASURING_R_UV:
        case CHAR_STATE_MEASURING_R_VW:
        case CHAR_STATE_MEASURING_R_WU:      state_name = "RESISTANCE"; break;
        case CHAR_STATE_ALIGNMENT_STICTION:  state_name = "ALIGNMENT"; break;
        case CHAR_STATE_DYNAMIC_SPIN_50HZ:
        case CHAR_STATE_DYNAMIC_SPIN_100HZ:  state_name = "DYNAMIC_SPIN"; break;
        case CHAR_STATE_CALCULATING:         state_name = "CALCULATING"; break;
        case CHAR_STATE_COMPLETE:            state_name = "COMPLETE"; break;
        case CHAR_STATE_ABORTED:             state_name = "ABORTED"; break;
        default: break;
    }
    ctx.replyPrintf("%s,%u", state_name, pct);
    return SCPI_RES_OK;
}

static ScpiStatus handle_test_char_results(ScpiContext& ctx) {
    const CharResults_t* res = MotorChar_GetResults();
    ctx.replyPrintf("%.3f,%.1f,%.2f,%.1f,%.1f",
                    res->r_phase_ohm,
                    res->kv_rating,
                    res->ke_v_krpm,
                    res->recommended_base_duty,
                    res->recommended_vf_slope);
    return SCPI_RES_OK;
}

static ScpiStatus handle_test_char_apply(ScpiContext& ctx) {
    (void)ctx;
    MotorChar_ApplyResults();
    return SCPI_RES_OK;
}

static ScpiStatus handle_fault_reset(ScpiContext& ctx) {
    (void)ctx;
    Motor_ResetFault();
    return SCPI_RES_OK;
}

static ScpiStatus handle_fault_state(ScpiContext& ctx) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    ctx.replyBool(telem.fault_active);
    return SCPI_RES_OK;
}

/* ============================================================================
 * Flywheel Command Table
 * ============================================================================ */

static const ScpiCommandDef s_flywheel_commands[] = {
    /* System Communication Mode */
    { "SYSTem:COMMunication:MODE",        SCPI_CMD_AND_QUERY, &handle_comm_mode },

    /* Motor Drive & Output Subsystem */
    { "OUTPut[:STATe]",                   SCPI_CMD_AND_QUERY, &handle_output_state },
    { "OUTPut:BRAKe",                     SCPI_CMD_ONLY,      &handle_output_brake },
    { "SOURce:MOTor:SPEEd[:COMManded]",   SCPI_CMD_AND_QUERY, &handle_motor_speed },
    { "SOURce:MOTor:DIRection",           SCPI_CMD_AND_QUERY, &handle_motor_direction },
    { "SOURce:MOTor:MODE",                SCPI_CMD_AND_QUERY, &handle_motor_mode },
    { "SOURce:MOTor:DUTY",                SCPI_CMD_AND_QUERY, &handle_motor_duty },
    { "SOURce:MOTor:SLEW:ACCEL",          SCPI_CMD_AND_QUERY, &handle_slew_accel },
    { "SOURce:MOTor:SLEW:DECEL",          SCPI_CMD_AND_QUERY, &handle_slew_decel },
    { "SOURce:MOTor:SLEW?",               SCPI_QUERY_ONLY,    &handle_slew_query_both },
    { "SOURce:MOTor:CURRent:LIMit",       SCPI_CMD_AND_QUERY, &handle_current_limit },
    { "SOURce:MOTor:POTentiometer[:STATe]", SCPI_CMD_AND_QUERY, &handle_pot_control },
    { "SOURce:MOTor:PROFile:SELect",      SCPI_CMD_AND_QUERY, &handle_profile_select },
    { "SOURce:MOTor:PROFile:NAME?",       SCPI_QUERY_ONLY,    &handle_profile_name },
    { "SOURce:MOTor:PROProfile:CATalog?", SCPI_QUERY_ONLY,    &handle_profile_catalog },

    /* Measurement Subsystem */
    { "MEASure:VOLTage[:DC]?",            SCPI_QUERY_ONLY,    &handle_meas_vbus },
    { "MEASure:TEMPerature?",             SCPI_QUERY_ONLY,    &handle_meas_temp },
    { "MEASure:SPEEd:OPTIcal?",           SCPI_QUERY_ONLY,    &handle_meas_optical_rpm },
    { "MEASure:SPEEd:HALL?",              SCPI_QUERY_ONLY,    &handle_meas_hall_rpm },
    { "MEASure:SPEEd[:ACTUal]?",          SCPI_QUERY_ONLY,    &handle_meas_actual_rpm },
    { "MEASure:CURRent:PEAK?",            SCPI_QUERY_ONLY,    &handle_meas_current_peak },
    { "MEASure:CURRent:PHASe:ALL?",       SCPI_QUERY_ONLY,    &handle_meas_phase_currents },
    { "MEASure:CURRent:DQ?",              SCPI_QUERY_ONLY,    &handle_meas_current_dq },
    { "MEASure:PULSe:OPTIcal?",           SCPI_QUERY_ONLY,    &handle_meas_optical_pulses },
    { "MEASure:TELEmetry:ALL?",           SCPI_QUERY_ONLY,    &handle_meas_telemetry_all },

    /* Diagnostics & Characterization Subsystem */
    { "TEST:STALL?",                      SCPI_QUERY_ONLY,    &handle_test_stall },
    { "TEST:SLIP?",                       SCPI_QUERY_ONLY,    &handle_test_slip },
    { "TEST:OPTIcal:RESet",               SCPI_CMD_ONLY,      &handle_test_reset_optical },
    { "TEST:CALibrate:CURRent",           SCPI_CMD_ONLY,      &handle_test_calibrate_current },
    { "TEST:CHARacterize:START",          SCPI_CMD_ONLY,      &handle_test_char_start },
    { "TEST:CHARacterize:ABORt",          SCPI_CMD_ONLY,      &handle_test_char_abort },
    { "TEST:CHARacterize:STATe?",         SCPI_QUERY_ONLY,    &handle_test_char_state },
    { "TEST:CHARacterize:RESults?",       SCPI_QUERY_ONLY,    &handle_test_char_results },
    { "TEST:CHARacterize:APPLy",          SCPI_CMD_ONLY,      &handle_test_char_apply },
    { "FAULt:RESet",                      SCPI_CMD_ONLY,      &handle_fault_reset },
    { "FAULt:STATe?",                     SCPI_QUERY_ONLY,    &handle_fault_state },
};

static void flywheel_scpi_reset_cb(void) {
    Motor_Stop();
    Motor_ResetFault();
    MotorProfiles_Select(0);
}

static int flywheel_scpi_selftest_cb(void) {
    MotorTelemetry_t telem;
    Motor_GetTelemetry(&telem);
    if (telem.fault_active) {
        return -240; /* Hardware error */
    }
    if (telem.vbus_v < 6.0f) {
        return -240; /* Undervoltage / power disconnected */
    }
    return 0; /* Pass */
}

/* ============================================================================
 * C Interface Implementation
 * ============================================================================ */

extern "C" {

void FlywheelScpi_Init(void) {
    const PowerStageCapabilities_t* caps = PowerStage_GetCapabilities();
    char idn_buf[80];
    snprintf(idn_buf, sizeof(idn_buf), "STMicroelectronics,STM32G474RE+%s,0,v1.2.0",
             caps ? caps->board_name : "IHM07M1");
    s_parser.setIdn(idn_buf);

    s_parser.registerCommands(s_flywheel_commands, 
                             sizeof(s_flywheel_commands) / sizeof(s_flywheel_commands[0]));
    s_parser.setResetCallback(&flywheel_scpi_reset_cb);
    s_parser.setSelfTestCallback(&flywheel_scpi_selftest_cb);
}

void FlywheelScpi_Process(void) {
    s_parser.process();
}

void FlywheelScpi_SetMode(CommMode_t mode) {
    console_set_mode(mode);
}

CommMode_t FlywheelScpi_GetMode(void) {
    return console_get_mode();
}

} /* extern "C" */
