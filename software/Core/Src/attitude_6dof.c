#include "attitude_6dof.h"
#include <math.h>
#include <string.h>

#define RAD_TO_DEG 57.2957795131f
#define CENTIDPS_TO_RAD 0.0001745329252f
#define ACC_MIN_MG 800.0f
#define ACC_MAX_MG 1200.0f
#define ACC_FEEDBACK_KP 2.0f

static void angles(Attitude6Dof *s)
{
    float w = s->q[0], x = s->q[1], y = s->q[2], z = s->q[3];
    float sin_pitch = 2.0f * (w*y - z*x);
    if (sin_pitch > 1.0f) sin_pitch = 1.0f;
    if (sin_pitch < -1.0f) sin_pitch = -1.0f;
    s->roll_deg = atan2f(2.0f * (w*x + y*z),
                         1.0f - 2.0f * (x*x + y*y)) * RAD_TO_DEG;
    s->pitch_deg = -asinf(sin_pitch) * RAD_TO_DEG;
    s->yaw_deg = -atan2f(2.0f * (w*z + x*y),
                         1.0f - 2.0f * (y*y + z*z)) * RAD_TO_DEG;
}

void Attitude6Dof_Reset(Attitude6Dof *s)
{
    memset(s, 0, sizeof(*s));
    s->q[0] = 1.0f;
}

int Attitude6Dof_Init(Attitude6Dof *s, const int32_t acc_mg[3])
{
    float ax = (float)acc_mg[0], ay = (float)acc_mg[1], az = (float)acc_mg[2];
    float norm = sqrtf(ax*ax + ay*ay + az*az);
    if (!isfinite(norm) || norm < ACC_MIN_MG || norm > ACC_MAX_MG) return 0;
    float roll = atan2f(ay, az);
    float pitch = atan2f(-ax, sqrtf(ay*ay + az*az));
    float cr = cosf(0.5f*roll), sr = sinf(0.5f*roll);
    float cp = cosf(0.5f*pitch), sp = sinf(0.5f*pitch);
    /* Start at relative yaw zero; tilt is observable from gravity. */
    s->q[0] = cr*cp; s->q[1] = sr*cp;
    s->q[2] = cr*sp; s->q[3] = -sr*sp;
    s->ready = 1;
    angles(s);
    return 1;
}

void Attitude6Dof_Update(Attitude6Dof *s, const int32_t acc_mg[3],
                         const int32_t gyro_centidps[3], float dt_s)
{
    if (!s->ready || !isfinite(dt_s) || dt_s <= 0.0f || dt_s > 0.1f) return;
    float gx = (float)gyro_centidps[0] * CENTIDPS_TO_RAD;
    float gy = (float)gyro_centidps[1] * CENTIDPS_TO_RAD;
    float gz = (float)gyro_centidps[2] * CENTIDPS_TO_RAD;
    float ax = (float)acc_mg[0], ay = (float)acc_mg[1], az = (float)acc_mg[2];
    float norm = sqrtf(ax*ax + ay*ay + az*az);
    if (isfinite(norm) && norm >= ACC_MIN_MG && norm <= ACC_MAX_MG) {
        ax /= norm; ay /= norm; az /= norm;
        float w = s->q[0], x = s->q[1], y = s->q[2], z = s->q[3];
        /* Predicted gravity/up direction in body coordinates. */
        float vx = 2.0f * (x*z - w*y);
        float vy = 2.0f * (w*x + y*z);
        float vz = 1.0f - 2.0f * (x*x + y*y);
        gx += ACC_FEEDBACK_KP * (ay*vz - az*vy);
        gy += ACC_FEEDBACK_KP * (az*vx - ax*vz);
        gz += ACC_FEEDBACK_KP * (ax*vy - ay*vx);
    }
    float w = s->q[0], x = s->q[1], y = s->q[2], z = s->q[3];
    s->q[0] += 0.5f * (-x*gx - y*gy - z*gz) * dt_s;
    s->q[1] += 0.5f * (w*gx + y*gz - z*gy) * dt_s;
    s->q[2] += 0.5f * (w*gy - x*gz + z*gx) * dt_s;
    s->q[3] += 0.5f * (w*gz + x*gy - y*gx) * dt_s;
    norm = sqrtf(s->q[0]*s->q[0] + s->q[1]*s->q[1] +
                 s->q[2]*s->q[2] + s->q[3]*s->q[3]);
    if (!isfinite(norm) || norm < 0.5f) { Attitude6Dof_Reset(s); return; }
    for (unsigned i = 0; i < 4; ++i) s->q[i] /= norm;
    angles(s);
}
