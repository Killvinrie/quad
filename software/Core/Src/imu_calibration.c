#include "imu_calibration.h"
#include <string.h>

#define ACC_SPREAD_LIMIT_MG 120
#define GYRO_SPREAD_LIMIT_CENTIDPS 500
#define LEVEL_XY_LIMIT_MG 200
#define LEVEL_Z_MIN_MG 850
#define LEVEL_Z_MAX_MG 1150

static int32_t absolute(int32_t value)
{
    return value < 0 ? -value : value;
}

void ImuCalibration_Reset(ImuCalibration *cal)
{
    memset(cal, 0, sizeof(*cal));
}

/* A full stable window is required. Any large change restarts the window;
 * a tilted or accelerating board cannot establish the level reference. */
int ImuCalibration_Add(ImuCalibration *cal, const int32_t acc[3],
                       const int32_t gyro[3])
{
    int32_t sample[6] = {acc[0], acc[1], acc[2], gyro[0], gyro[1], gyro[2]};
    if (cal->ready) return 1;

    if (cal->count == 0) {
        for (unsigned i = 0; i < 6; ++i) {
            cal->low[i] = cal->high[i] = sample[i];
            cal->sum[i] = sample[i];
        }
        cal->count = 1;
        return 0;
    }

    for (unsigned i = 0; i < 6; ++i) {
        int32_t low = sample[i] < cal->low[i] ? sample[i] : cal->low[i];
        int32_t high = sample[i] > cal->high[i] ? sample[i] : cal->high[i];
        int32_t limit = i < 3 ? ACC_SPREAD_LIMIT_MG : GYRO_SPREAD_LIMIT_CENTIDPS;
        if ((int64_t)high - low > limit) {
            cal->count = 0;
            return ImuCalibration_Add(cal, acc, gyro);
        }
    }
    for (unsigned i = 0; i < 6; ++i) {
        if (sample[i] < cal->low[i]) cal->low[i] = sample[i];
        if (sample[i] > cal->high[i]) cal->high[i] = sample[i];
        cal->sum[i] += sample[i];
    }
    ++cal->count;
    if (cal->count < IMU_CALIBRATION_SAMPLES) return 0;

    for (unsigned i = 0; i < 6; ++i)
        cal->bias[i] = (int32_t)(cal->sum[i] / IMU_CALIBRATION_SAMPLES);

    if (absolute(cal->bias[0]) > LEVEL_XY_LIMIT_MG ||
        absolute(cal->bias[1]) > LEVEL_XY_LIMIT_MG ||
        absolute(cal->bias[2]) < LEVEL_Z_MIN_MG ||
        absolute(cal->bias[2]) > LEVEL_Z_MAX_MG) {
        cal->count = 0;
        return 0;
    }

    /* The Z accelerometer must retain gravity, including its measured sign. */
    cal->bias[2] -= cal->bias[2] < 0 ? -1000 : 1000;
    cal->ready = 1;
    return 1;
}

void ImuCalibration_Apply(const ImuCalibration *cal, int32_t acc[3],
                          int32_t gyro[3])
{
    if (!cal->ready) return;
    for (unsigned i = 0; i < 3; ++i) {
        acc[i] -= cal->bias[i];
        gyro[i] -= cal->bias[3 + i];
    }
}

int ImuCalibration_LargeGyroBias(const ImuCalibration *cal)
{
    if (!cal->ready) return 0;
    for (unsigned i = 3; i < 6; ++i)
        if (absolute(cal->bias[i]) > IMU_LARGE_GYRO_BIAS_CENTIDPS) return 1;
    return 0;
}
