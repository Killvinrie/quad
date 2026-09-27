#ifndef MOTOR_CONFIG_H
#define MOTOR_CONFIG_H

/* X frame viewed from above, nose forward:
 *            FRONT
 *         FL       FR
 *         RL       RR
 * PCB nets: MOTOR1=CH1/PB4, MOTOR2=CH2/PB5,
 *           MOTOR3=CH3/PB0, MOTOR4=CH4/PB1.
 * Installed motors: FL=MOTOR2, FR=MOTOR1, RR=MOTOR3, RL=MOTOR4.
 * Verify correction directions with propellers removed before enabling PID.
 * Values 0..3 correspond to CH1..CH4. */
#define MOTOR_OUTPUT_FL 1U
#define MOTOR_OUTPUT_FR 0U
#define MOTOR_OUTPUT_RR 2U
#define MOTOR_OUTPUT_RL 3U
#ifndef MOTOR_STABILIZATION_ENABLED
#define MOTOR_STABILIZATION_ENABLED 1
#endif

#if MOTOR_STABILIZATION_ENABLED && ( \
    MOTOR_OUTPUT_FL > 3U || MOTOR_OUTPUT_FR > 3U || \
    MOTOR_OUTPUT_RR > 3U || MOTOR_OUTPUT_RL > 3U || \
    MOTOR_OUTPUT_FL == MOTOR_OUTPUT_FR || \
    MOTOR_OUTPUT_FL == MOTOR_OUTPUT_RR || \
    MOTOR_OUTPUT_FL == MOTOR_OUTPUT_RL || \
    MOTOR_OUTPUT_FR == MOTOR_OUTPUT_RR || \
    MOTOR_OUTPUT_FR == MOTOR_OUTPUT_RL || \
    MOTOR_OUTPUT_RR == MOTOR_OUTPUT_RL)
#error "Motor output mapping must be a permutation of 0,1,2,3"
#endif

#endif
