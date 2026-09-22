#include "bmp388_math.h"
#include <math.h>
static unsigned u16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static int s16(const uint8_t *p) { unsigned n = u16(p); return n >= 32768 ? (int)n - 65536 : (int)n; }
static int s8(uint8_t n) { return n >= 128 ? (int)n - 256 : n; }

void Bmp388_DecodeCalibration(const uint8_t r[21], BmpCalibration *c)
{
    c->t[0] = u16(r) * 256.0;
    c->t[1] = u16(r + 2) / 1073741824.0;
    c->t[2] = s8(r[4]) / 281474976710656.0;
    c->p[0] = (s16(r + 5) - 16384) / 1048576.0;
    c->p[1] = (s16(r + 7) - 16384) / 536870912.0;
    c->p[2] = s8(r[9]) / 4294967296.0;
    c->p[3] = s8(r[10]) / 137438953472.0;
    c->p[4] = u16(r + 11) * 8.0;
    c->p[5] = u16(r + 13) / 64.0;
    c->p[6] = s8(r[15]) / 256.0;
    c->p[7] = s8(r[16]) / 32768.0;
    c->p[8] = s16(r + 17) / 281474976710656.0;
    c->p[9] = s8(r[19]) / 281474976710656.0;
    c->p[10] = s8(r[20]) / 36893488147419103232.0;
}
int Bmp388_Compensate(const BmpCalibration *c, uint32_t raw_t, uint32_t raw_p,
                      int32_t *temperature_centi_c, int32_t *pressure_pa)
{
    double d = raw_t - c->t[0];
    double t = d * c->t[1] + d * d * c->t[2];
    double r = raw_p;
    const double *p = c->p;
    double pressure = p[4] + t * (p[5] + t * (p[6] + t * p[7])) +
        r * (p[0] + t * (p[1] + t * (p[2] + t * p[3]))) +
        r * r * (p[8] + t * p[9] + r * p[10]);
    if (!isfinite(t) || !isfinite(pressure) || t < -40 || t > 85 ||
        pressure < 30000 || pressure > 125000) return 0;
    *temperature_centi_c = (int32_t)(t * 100 + (t < 0 ? -0.5 : 0.5));
    *pressure_pa = (int32_t)(pressure + 0.5);
    return 1;
}
