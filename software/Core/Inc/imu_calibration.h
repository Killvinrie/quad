#ifndef IMU_CALIBRATION_H
#define IMU_CALIBRATION_H

#include <stdint.h>

/* Input/output units: acceleration in mg, angular rate in 0.01 degree/s. */
#define IMU_CALIBRATION_SAMPLES 128U
#define IMU_LARGE_GYRO_BIAS_CENTIDPS 2000

typedef struct {
    int64_t sum[6];
    int32_t low[6];
    int32_t high[6];
    int32_t bias[6];
    uint16_t count;
    uint8_t ready;
} ImuCalibration;

void ImuCalibration_Reset(ImuCalibration *cal);
int ImuCalibration_Add(ImuCalibration *cal, const int32_t acc[3],
                       const int32_t gyro[3]);
void ImuCalibration_Apply(const ImuCalibration *cal, int32_t acc[3],
                          int32_t gyro[3]);
int ImuCalibration_LargeGyroBias(const ImuCalibration *cal);

#endif
