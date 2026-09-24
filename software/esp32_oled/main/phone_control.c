#include "phone_control.h"
#include <stdio.h>
#include <string.h>

int PhoneControl_Parse(const char *data, size_t length, PhoneInput *out)
{
    unsigned value[6] = {0};
    size_t pos = 0;
    if (!data || !out || length == 0 || length > 64) return 0;
    for (unsigned field = 0; field < 6; ++field) {
        unsigned digits = 0;
        while (pos < length && data[pos] >= '0' && data[pos] <= '9') {
            value[field] = value[field] * 10U + (unsigned)(data[pos++] - '0');
            if (++digits > 4 || value[field] > 4095U) return 0;
        }
        if (!digits) return 0;
        if (field < 5) {
            if (pos >= length || data[pos++] != ',') return 0;
        }
    }
    if (pos != length || value[4] > 1U || value[5] > 1U) return 0;
    for (unsigned i = 0; i < CONTROL_AXIS_COUNT; ++i)
        out->axis[i] = (uint16_t)value[i];
    out->show_sensor = (uint8_t)value[4];
    out->arm = (uint8_t)value[5];
    out->sequence = 0;
    return 1;
}

void PhoneControl_Apply(PhoneControl *state, const PhoneInput *input,
                        uint32_t now_tick)
{
    state->latest = *input;
    state->last_rx_tick = now_tick;
    state->seen = 1;
}

int PhoneControl_Online(const PhoneControl *state, uint32_t now_tick,
                        uint32_t timeout_ticks)
{
    return state->seen &&
           (uint32_t)(now_tick - state->last_rx_tick) <= timeout_ticks;
}

void PhoneControl_Encode(PhoneControl *state,
                         uint8_t frame[CONTROL_FRAME_SIZE])
{
    ControlCommand command = {0};
    command.sequence = state->sequence++;
    state->latest.sequence = command.sequence;
    for (unsigned i = 0; i < CONTROL_AXIS_COUNT; ++i)
        command.axis[i] = state->latest.axis[i];
    command.flags = state->latest.arm ? CONTROL_FLAG_ARM : 0;
    control_encode(frame, &command);
}

void PhoneControl_Render(const PhoneControl *state, uint32_t now_tick,
                         uint32_t timeout_ticks, const char *ssid,
                         char rows[DISPLAY_ROWS][DISPLAY_COLS + 1])
{
    memset(rows, 0, sizeof(char[DISPLAY_ROWS][DISPLAY_COLS + 1]));
    if (!PhoneControl_Online(state, now_tick, timeout_ticks)) {
        strcpy(rows[0], state->seen ? "PHONE LINK LOST" : "PHONE CTRL WAIT");
        snprintf(rows[1], DISPLAY_COLS + 1, "SSID:%s", ssid);
        strcpy(rows[2], "OPEN 192.168.4.1");
        strcpy(rows[4], "START ON PHONE");
        return;
    }
    strcpy(rows[0], state->latest.arm ? "MOTOR ARM REQUEST" : "MOTOR DISARMED");
    snprintf(rows[1], DISPLAY_COLS + 1, "YAW:%4u",
             (unsigned)state->latest.axis[0]);
    snprintf(rows[2], DISPLAY_COLS + 1, "THR:%4u",
             (unsigned)state->latest.axis[1]);
    snprintf(rows[3], DISPLAY_COLS + 1, "PITCH:%4u",
             (unsigned)state->latest.axis[2]);
    snprintf(rows[4], DISPLAY_COLS + 1, "ROLL:%4u",
             (unsigned)state->latest.axis[3]);
    snprintf(rows[6], DISPLAY_COLS + 1, "SEQ:%03u",
             (unsigned)state->latest.sequence);
    strcpy(rows[7], "WEB: SWITCH DISPLAY");
}
