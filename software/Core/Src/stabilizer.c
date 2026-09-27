#include "stabilizer.h"
#include "motor_logic.h"
#include <math.h>
#include <string.h>

static float limit(float value, float maximum)
{
    if (value > maximum) return maximum;
    if (value < -maximum) return -maximum;
    return value;
}

void Stabilizer_Reset(Stabilizer *state)
{
    memset(state, 0, sizeof(*state));
}

int Stabilizer_Step(Stabilizer *state, const ControlCommand *command,
                    const SensorAttitude *sample, float dt_s,
                    uint16_t output_us[4])
{
    if (!state || !command || !sample || !output_us ||
        !isfinite(dt_s) || dt_s < 0.005f || dt_s > 0.1f ||
        !isfinite(sample->roll_deg) || !isfinite(sample->pitch_deg) ||
        !isfinite(sample->roll_rate_dps) ||
        !isfinite(sample->pitch_rate_dps) ||
        !isfinite(sample->yaw_rate_dps) ||
        (STAB_MAX_TILT_DEG > 0.0f &&
         (fabsf(sample->roll_deg) > STAB_MAX_TILT_DEG ||
          fabsf(sample->pitch_deg) > STAB_MAX_TILT_DEG))) {
        if (state) Stabilizer_Reset(state);
        return 0;
    }
    if (command->axis[1] == 0U) {
        Stabilizer_Reset(state);
        for (unsigned i = 0; i < 4; ++i) output_us[i] = MOTOR_MIN_US;
        return 1;
    }
    float target_roll = limit(((float)command->axis[3] - 2048.0f) *
                              (20.0f / 2047.0f), 20.0f);
    /* Browser stick up means forward/nose-down, while displayed pitch + is
     * nose-up, so the pitch command has the opposite sign. */
    float target_pitch = limit((2048.0f - (float)command->axis[2]) *
                               (20.0f / 2047.0f), 20.0f);
    float target_yaw_rate = limit(((float)command->axis[0] - 2048.0f) *
                                  (45.0f / 2047.0f), 45.0f);
    float desired_rate[3] = {
        limit(STAB_ANGLE_P * (target_roll - sample->roll_deg), 80.0f),
        limit(STAB_ANGLE_P * (target_pitch - sample->pitch_deg), 80.0f),
        target_yaw_rate
    };
    float rate[3] = {sample->roll_rate_dps, sample->pitch_rate_dps,
                     sample->yaw_rate_dps};
    float correction[3];
    for (unsigned i = 0; i < 3; ++i) {
        float error = desired_rate[i] - rate[i];
        state->integral[i] = limit(state->integral[i] + error * dt_s, 200.0f);
        if (state->initialized) {
            float derivative = (rate[i] - state->last_rate[i]) / dt_s;
            state->derivative[i] = 0.7f * state->derivative[i] +
                                   0.3f * derivative;
        }
        state->last_rate[i] = rate[i];
        float kp = i == 2 ? STAB_YAW_RATE_P : STAB_RATE_P;
        float ki = i == 2 ? STAB_YAW_RATE_I : STAB_RATE_I;
        float kd = i == 2 ? 0.0f : STAB_RATE_D;
        correction[i] = limit(kp * error + ki * state->integral[i] -
                              kd * state->derivative[i], 120.0f);
    }
    state->initialized = 1;
    float base = (float)MOTOR_MIN_US +
                 (float)command->axis[1] *
                 ((float)(MOTOR_MAX_US - MOTOR_MIN_US) / 4095.0f);
    float r = correction[0], p = correction[1], y = correction[2];
    /* X geometry. Positive roll lifts left, positive pitch lifts front.
     * FL/RR spin CW; FR/RL spin CCW, viewed from above. Increasing the CCW
     * pair creates positive (right-turning) body yaw. */
    float mixed[4] = {base + r + p - y, base - r + p + y,
                      base - r - p - y, base + r - p + y};
    for (unsigned i = 0; i < 4; ++i) {
        if (mixed[i] < MOTOR_MIN_US) mixed[i] = MOTOR_MIN_US;
        if (mixed[i] > MOTOR_MAX_US) mixed[i] = MOTOR_MAX_US;
        output_us[i] = (uint16_t)(mixed[i] + 0.5f);
    }
    return 1;
}
