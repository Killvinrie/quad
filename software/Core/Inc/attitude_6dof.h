#ifndef ATTITUDE_6DOF_H
#define ATTITUDE_6DOF_H

#include <stdint.h>

/* Body axes: X forward, Y left, Z up. Quaternion rotates body to world.
 * Output signs: roll + left side up, pitch + nose up, yaw + right turn.
 * Yaw is relative to the orientation at initialization (no magnetometer). */
typedef struct {
    float q[4];
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
    uint8_t ready;
} Attitude6Dof;

void Attitude6Dof_Reset(Attitude6Dof *state);
int Attitude6Dof_Init(Attitude6Dof *state, const int32_t acc_mg[3]);
void Attitude6Dof_Update(Attitude6Dof *state, const int32_t acc_mg[3],
                         const int32_t gyro_centidps[3], float dt_s);

#endif
