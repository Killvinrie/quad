#ifndef MOTOR_APP_H
#define MOTOR_APP_H

#include <stdint.h>

typedef struct {
    uint16_t pwm_us[4]; /* CH1..CH4 compare values, in microseconds. */
    uint8_t armed;
    uint8_t stabilizing;
} MotorAppStatus;

void MotorApp_Init(void);
void MotorApp_StartWatchdog(void);
void MotorApp_Poll(void);
void MotorApp_GetStatus(MotorAppStatus *out);

#endif
