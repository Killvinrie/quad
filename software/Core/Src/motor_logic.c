#include "motor_logic.h"

void MotorLogic_Init(MotorLogic *state)
{
    state->armed = 0;
    state->saw_disarmed = 0;
    state->arm_was_high = 0;
}

void MotorLogic_ForceDisarm(MotorLogic *state)
{
    state->armed = 0;
    state->saw_disarmed = 0;
    state->arm_was_high = 1;
}

uint16_t MotorLogic_Update(MotorLogic *state, const ControlCommand *command,
                           int link_valid)
{
    if (!link_valid || !command) {
        MotorLogic_ForceDisarm(state);
        return MOTOR_MIN_US;
    }
    if (!(command->flags & CONTROL_FLAG_ARM)) {
        state->armed = 0;
        state->saw_disarmed = 1;
        state->arm_was_high = 0;
        return MOTOR_MIN_US;
    }
    if (!state->arm_was_high && state->saw_disarmed &&
        command->axis[1] <= MOTOR_ARM_THROTTLE_MAX &&
        command->axis[0] >= 2048U - MOTOR_CENTER_TOLERANCE &&
        command->axis[0] <= 2048U + MOTOR_CENTER_TOLERANCE &&
        command->axis[2] >= 2048U - MOTOR_CENTER_TOLERANCE &&
        command->axis[2] <= 2048U + MOTOR_CENTER_TOLERANCE &&
        command->axis[3] >= 2048U - MOTOR_CENTER_TOLERANCE &&
        command->axis[3] <= 2048U + MOTOR_CENTER_TOLERANCE)
        state->armed = 1;
    state->arm_was_high = 1;
    if (!state->armed) return MOTOR_MIN_US;
    uint16_t throttle = command->axis[1];
    if (throttle > 4095U) throttle = 4095U;
    return (uint16_t)(MOTOR_MIN_US +
                      ((uint32_t)throttle * (MOTOR_MAX_US - MOTOR_MIN_US) +
                       2047U) / 4095U);
}
