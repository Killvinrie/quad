#ifndef QUAD_PHONE_CONTROL_H
#define QUAD_PHONE_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include "control_link.h"
#include "display_link.h"

typedef struct {
    uint16_t axis[CONTROL_AXIS_COUNT]; /* yaw, throttle, pitch, roll */
    uint8_t show_sensor;
    uint8_t sequence; /* Set by ESP32 after UART frame encoding. */
} PhoneInput;

typedef struct {
    PhoneInput latest;
    uint32_t last_rx_tick;
    uint8_t seen;
    uint8_t sequence;
} PhoneControl;

/* Exactly five comma-separated decimal fields: yaw,thr,pitch,roll,view.
 * view=0 shows phone values on OLED; view=1 shows STM32 sensor pages. */
int PhoneControl_Parse(const char *data, size_t length, PhoneInput *out);
void PhoneControl_Apply(PhoneControl *state, const PhoneInput *input,
                        uint32_t now_tick);
int PhoneControl_Online(const PhoneControl *state, uint32_t now_tick,
                        uint32_t timeout_ticks);
void PhoneControl_Encode(PhoneControl *state,
                         uint8_t frame[CONTROL_FRAME_SIZE]);
void PhoneControl_Render(const PhoneControl *state, uint32_t now_tick,
                         uint32_t timeout_ticks, const char *ssid,
                         char rows[DISPLAY_ROWS][DISPLAY_COLS + 1]);

#endif
