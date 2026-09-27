#ifndef STABILIZER_H
#define STABILIZER_H

#include <stdint.h>
#include "control_link.h"
#include "sensor_app.h"

/* Initial bench-only gains, in microseconds per degree/s, etc. These are not
 * flight-tuned. Yaw correction starts disabled until motor spin is checked. */
#define STAB_ANGLE_P 0.0f
#define STAB_RATE_P 1.0f
#define STAB_RATE_I 0.00f
#define STAB_RATE_D 0.00f
#define STAB_YAW_RATE_P 0.0f
#define STAB_YAW_RATE_I 0.0f
/* 0 disables angle-based disarming; set a positive value to restore it. */
#define STAB_MAX_TILT_DEG 0.0f

typedef struct {
    float integral[3];
    float derivative[3];
    float last_rate[3];
    uint8_t initialized;
} Stabilizer;

void Stabilizer_Reset(Stabilizer *state);
/* Returns four logical motor pulse widths: FL, FR, RR, RL. */
int Stabilizer_Step(Stabilizer *state, const ControlCommand *command,
                    const SensorAttitude *sample, float dt_s,
                    uint16_t output_us[4]);

#endif
