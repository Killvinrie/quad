#ifndef SENSOR_APP_H
#define SENSOR_APP_H
#include <stdint.h>

typedef struct {
    float roll_deg, pitch_deg;
    float roll_rate_dps, pitch_rate_dps, yaw_rate_dps;
    uint32_t tick_ms;
} SensorAttitude;

void SensorApp_Init(void);
void SensorApp_Poll(void);
void SensorApp_GpsIRQ(void);
void SensorApp_LinkIRQ(void);
int SensorApp_GetAttitude(SensorAttitude *out, uint32_t now);
#endif
