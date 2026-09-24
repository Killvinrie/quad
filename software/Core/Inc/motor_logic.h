#ifndef MOTOR_LOGIC_H
#define MOTOR_LOGIC_H

#include <stdint.h>
#include "control_link.h"

/* Conservative initial range. Calibrate the actual ESCs to these endpoints. */
#define MOTOR_MIN_US 1100U
#define MOTOR_MAX_US 1940U
#define MOTOR_ARM_THROTTLE_MAX 40U
#define MOTOR_CENTER_TOLERANCE 160U

typedef struct {
    uint8_t armed;
    uint8_t saw_disarmed;
    uint8_t arm_was_high;
} MotorLogic;

void MotorLogic_Init(MotorLogic *state);
void MotorLogic_ForceDisarm(MotorLogic *state);
uint16_t MotorLogic_Update(MotorLogic *state, const ControlCommand *command,
                           int link_valid);

#endif
