#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "display_link.h"
#include "control_link.h"
#include "gps_nmea.h"
#include "bmp388_math.h"
#include "imu_calibration.h"
#include "attitude_6dof.h"
#include "phone_control.h"

static void sentence(char *out, const char *body)
{
    unsigned crc = 0;
    for (const char *p = body; *p; ++p) crc ^= (unsigned char)*p;
    sprintf(out, "$%s*%02X\r\n", body, crc);
}
static void test_gps(void)
{
    char line[256];
    GpsFix fix = {0};
    strcpy(line, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    assert(Gps_ParseGga(line, &fix));
    assert(fix.quality == 1 && fix.satellites == 8 && fix.ns == 'N');
    assert(!strcmp(fix.latitude, "4807.038"));
    sentence(line, "GNGGA,123519,3456.789,S,12345.678,W,2,12,0.9,0,M,0,M,,");
    assert(Gps_ParseGga(line, &fix) && fix.ns == 'S' && fix.ew == 'W');
    GpsFix saved = fix;
    sentence(line, "GPGGA,123519,4867.038,N,01131.000,E,1,08,0.9,0,M,0,M,,");
    assert(!Gps_ParseGga(line, &fix)); /* minutes >= 60 */
    assert(!memcmp(&saved, &fix, sizeof(fix)));
    sentence(line, "GPGGA,123519,9100.000,N,01131.000,E,1,08,0.9,0,M,0,M,,");
    assert(!Gps_ParseGga(line, &fix));
    sentence(line, "GPGGA,123519,4807.038,N,18131.000,E,1,08,0.9,0,M,0,M,,");
    assert(!Gps_ParseGga(line, &fix));
    sentence(line, "GPGGA,123519,4807.038,,01131.000,E,1,08,0.9,0,M,0,M,,");
    assert(!Gps_ParseGga(line, &fix));
    sentence(line, "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,0,M,0,M,,");
    line[10] ^= 1;
    assert(!Gps_ParseGga(line, &fix));
    strcpy(line, "$GPGGA*");
    assert(!Gps_ParseGga(line, &fix));
    sentence(line, "GNRMC,123519,A,4807.038,N,01131.000,E,0,0,0,0,0");
    assert(!Gps_ParseGga(line, &fix));
    sentence(line, "GNGGA,123519,,,,,0,00,99.99,,,,,,");
    assert(Gps_ParseGga(line, &fix) && !fix.quality && !fix.latitude[0]);
}
static void test_frames(void)
{
    char rows[DISPLAY_ROWS][DISPLAY_COLS + 1] = {{0}}, out[DISPLAY_ROWS][DISPLAY_COLS + 1];
    uint8_t frame[DISPLAY_FRAME_SIZE];
    strcpy(rows[0], "MPU6050  ACC/GYRO"); strcpy(rows[7], "TEMP:-12.34C");
    display_encode(frame, rows);
    assert(display_crc((const uint8_t *)"123456789", 9) == 0x29b1);
    /* Every possible truncation, inserted noise, and byte corruption must
       recover by the end of the next intact frame. */
    for (unsigned cut = 0; cut < DISPLAY_FRAME_SIZE; ++cut) {
        DisplayParser p = {0};
        for (unsigned i = 0; i < cut; ++i) assert(!display_feed(&p, frame[i], out));
        for (unsigned i = 0; i < DISPLAY_FRAME_SIZE; ++i)
            assert(display_feed(&p, frame[i], out) == (i == DISPLAY_FRAME_SIZE - 1));
        assert(!strncmp(out[0], rows[0], strlen(rows[0])));
        assert(!strncmp(out[7], rows[7], strlen(rows[7])));
        assert(out[7][DISPLAY_COLS] == 0);
    }
    for (unsigned bad = 0; bad < DISPLAY_FRAME_SIZE; ++bad) {
        DisplayParser p = {0};
        for (unsigned i = 0; i < DISPLAY_FRAME_SIZE; ++i)
            assert(!display_feed(&p, frame[i] ^ (i == bad ? 0x01 : 0), out));
        for (unsigned i = 0; i < DISPLAY_FRAME_SIZE; ++i)
            assert(display_feed(&p, frame[i], out) == (i == DISPLAY_FRAME_SIZE - 1));
    }
    DisplayParser p = {0};
    for (unsigned i = 0; i < 10000; ++i)
        (void)display_feed(&p, (uint8_t)(i * 37), out);
    for (unsigned i = 0; i < DISPLAY_FRAME_SIZE; ++i)
        assert(display_feed(&p, frame[i], out) == (i == DISPLAY_FRAME_SIZE - 1));
}
static void test_bmp(void)
{
    BmpCalibration c = {0};
    int32_t temp = 0, pressure = 0;
    /* Analytic case with pressure temperature dependency, non-linear terms. */
    c.t[0] = 100; c.t[1] = 0.01; c.t[2] = 0.000001;
    c.p[4] = 100000; c.p[5] = 2; c.p[0] = 0.1;
    c.p[8] = 0.00001;
    assert(Bmp388_Compensate(&c, 1100, 1000, &temp, &pressure));
    assert(temp == 1100 && pressure == 100132);
    c.t[1] = -0.01;
    assert(Bmp388_Compensate(&c, 1100, 1000, &temp, &pressure) && temp == -900);
    c.p[4] = 200000;
    assert(!Bmp388_Compensate(&c, 1100, 1000, &temp, &pressure));
    c.p[4] = NAN;
    assert(!Bmp388_Compensate(&c, 1100, 1000, &temp, &pressure));
    /* Signed/unsigned coefficient decoding, including negative P1/P2. */
    uint8_t raw[21] = {1,2,3,4,255, 0,128,0,64,254,128,
                      0,128,0,255,255,128,0,128,255,128};
    Bmp388_DecodeCalibration(raw, &c);
    assert(c.t[0] == 513 * 256.0);
    assert(c.t[1] == 1027 / 1073741824.0);
    assert(c.t[2] == -1 / 281474976710656.0);
    assert(c.p[0] == -49152 / 1048576.0 && c.p[1] == 0);
    assert(c.p[4] == 32768 * 8.0 && c.p[5] == 65280 / 64.0);
    assert(c.p[8] == -32768 / 281474976710656.0);
    assert(c.p[10] == -128 / 36893488147419103232.0);
}
static void test_control(void)
{
    ControlCommand in = {0}, out = {0};
    uint8_t frame[CONTROL_FRAME_SIZE];
    in.sequence = 7;
    in.axis[0] = 1; in.axis[1] = 2048; in.axis[2] = 4095; in.axis[3] = 77;
    in.buttons = 0x55aa; in.flags = 3; in.battery_mv = 3710;
    control_encode(frame, &in);
    assert(control_crc((const uint8_t *)"123456789", 9) == 0x29b1);
    assert(control_decode(frame, &out));
    assert(!memcmp(&in, &out, sizeof(in)));
    ControlParser parser = {0};
    uint8_t parsed[CONTROL_FRAME_SIZE];
    for (unsigned i = 0; i < CONTROL_FRAME_SIZE; ++i)
        assert(control_feed(&parser, frame[i], parsed) ==
               (i == CONTROL_FRAME_SIZE - 1));
    assert(control_decode(parsed, &out) && out.sequence == in.sequence);
    frame[5] ^= 1;
    assert(!control_decode(frame, &out));
}
static void test_imu_calibration(void)
{
    ImuCalibration cal;
    int32_t acc[3] = {80, -40, 970};
    int32_t gyro[3] = {10000, 30, -20}; /* 100 deg/s X zero bias. */
    ImuCalibration_Reset(&cal);
    for (unsigned i = 0; i < IMU_CALIBRATION_SAMPLES; ++i)
        assert(ImuCalibration_Add(&cal, acc, gyro) ==
               (i == IMU_CALIBRATION_SAMPLES - 1));
    assert(ImuCalibration_LargeGyroBias(&cal));
    ImuCalibration_Apply(&cal, acc, gyro);
    assert(acc[0] == 0 && acc[1] == 0 && acc[2] == 1000);
    assert(gyro[0] == 0 && gyro[1] == 0 && gyro[2] == 0);

    acc[0] = 180; acc[1] = -40; acc[2] = 970;
    gyro[0] = 10500; gyro[1] = 30; gyro[2] = -20;
    ImuCalibration_Apply(&cal, acc, gyro);
    assert(acc[0] == 100 && acc[2] == 1000 && gyro[0] == 500);

    ImuCalibration_Reset(&cal);
    acc[0] = 0; acc[1] = 0; acc[2] = -980;
    gyro[0] = 0; gyro[1] = 0; gyro[2] = 0;
    for (unsigned i = 0; i < IMU_CALIBRATION_SAMPLES; ++i)
        (void)ImuCalibration_Add(&cal, acc, gyro);
    assert(cal.ready);
    ImuCalibration_Apply(&cal, acc, gyro);
    assert(acc[2] == -1000);

    ImuCalibration_Reset(&cal);
    acc[2] = 1000;
    for (unsigned i = 0; i < 32; ++i)
        (void)ImuCalibration_Add(&cal, acc, gyro);
    acc[0] = 450;
    assert(!ImuCalibration_Add(&cal, acc, gyro) && cal.count == 1);
    ImuCalibration_Reset(&cal);
    for (unsigned i = 0; i < IMU_CALIBRATION_SAMPLES; ++i)
        (void)ImuCalibration_Add(&cal, acc, gyro);
    assert(!cal.ready); /* A tilted board must not set the level reference. */
}
static void test_attitude(void)
{
    Attitude6Dof s;
    int32_t flat[3] = {0, 0, 1000};
    int32_t still[3] = {0, 0, 0};
    int32_t yaw_rate[3] = {0, 0, 9000};
    int32_t roll_30[3] = {0, 500, 866};
    int32_t accelerated[3] = {1000, 0, 1000};
    Attitude6Dof_Reset(&s);
    assert(Attitude6Dof_Init(&s, flat));
    assert(fabsf(s.roll_deg) < 0.1f && fabsf(s.pitch_deg) < 0.1f);
    for (unsigned i = 0; i < 50; ++i)
        Attitude6Dof_Update(&s, flat, yaw_rate, 0.02f);
    assert(fabsf(s.yaw_deg + 90.0f) < 1.0f);
    Attitude6Dof_Reset(&s);
    int32_t nose_up_30[3] = {500, 0, 866};
    assert(Attitude6Dof_Init(&s, nose_up_30));
    assert(fabsf(s.pitch_deg - 30.0f) < 0.2f);
    Attitude6Dof_Reset(&s);
    assert(Attitude6Dof_Init(&s, roll_30));
    assert(fabsf(s.roll_deg - 30.0f) < 0.2f);
    for (unsigned i = 0; i < 100; ++i)
        Attitude6Dof_Update(&s, flat, still, 0.02f);
    assert(fabsf(s.roll_deg) < 5.0f); /* Gravity corrects gyro/tilt error. */
    Attitude6Dof_Reset(&s);
    assert(Attitude6Dof_Init(&s, flat));
    for (unsigned i = 0; i < 100; ++i)
        Attitude6Dof_Update(&s, accelerated, still, 0.02f);
    assert(fabsf(s.pitch_deg) < 0.1f); /* Ignore non-1g acceleration. */
}
static void test_phone_control(void)
{
    PhoneControl state = {0};
    PhoneInput input = {0};
    ControlCommand decoded = {0};
    uint8_t frame[CONTROL_FRAME_SIZE];
    char rows[DISPLAY_ROWS][DISPLAY_COLS + 1];
    const char *body = "1234,4095,2048,55,0";
    assert(PhoneControl_Parse(body, strlen(body), &input));
    assert(input.axis[0] == 1234 && input.axis[1] == 4095);
    assert(!PhoneControl_Parse("-1,0,0,0,0", strlen("-1,0,0,0,0"), &input));
    assert(!PhoneControl_Parse("4096,0,0,0,0", strlen("4096,0,0,0,0"), &input));
    assert(!PhoneControl_Parse("1,2,3,4,2", strlen("1,2,3,4,2"), &input));
    assert(!PhoneControl_Parse("1,2,3,4,0x", strlen("1,2,3,4,0x"), &input));
    PhoneControl_Apply(&state, &input, 100);
    assert(PhoneControl_Online(&state, 350, 250));
    PhoneControl_Encode(&state, frame);
    assert(control_decode(frame, &decoded));
    assert(decoded.axis[0] == 1234 && decoded.axis[1] == 4095);
    assert(decoded.axis[2] == 2048 && decoded.axis[3] == 55);
    assert(decoded.buttons == 0 && decoded.flags == 0);
    PhoneControl_Render(&state, 120, 250, "QUAD-CONTROL", rows);
    assert(strstr(rows[1], "1234") && strstr(rows[2], "4095"));
    assert(!PhoneControl_Online(&state, 351, 250));
    PhoneControl_Render(&state, 351, 250, "QUAD-CONTROL", rows);
    assert(strstr(rows[0], "LINK LOST") && rows[3][0] == 0);
}
int main(void)
{
    test_gps(); test_frames(); test_bmp(); test_control(); test_imu_calibration();
    test_attitude(); test_phone_control();
    puts("PASS: GPS, CRC, BMP388, IMU, phone control");
    return 0;
}
