#ifndef BMP388_MATH_H
#define BMP388_MATH_H
#include <stdint.h>
typedef struct { double t[3], p[11]; } BmpCalibration;
void Bmp388_DecodeCalibration(const uint8_t raw[21], BmpCalibration *c);
int Bmp388_Compensate(const BmpCalibration *c, uint32_t raw_t, uint32_t raw_p,
                      int32_t *temperature_centi_c, int32_t *pressure_pa);
#endif
