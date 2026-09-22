#ifndef GPS_NMEA_H
#define GPS_NMEA_H
#include <stdint.h>
typedef struct {
    uint8_t quality, satellites;
    /* Preserve NMEA ddmm.mmmm / dddmm.mmmm for lossless display. */
    char latitude[16], longitude[16], ns, ew;
} GpsFix;
/* Mutable complete line, optional CR/LF. Returns 1 only for valid GGA. */
int Gps_ParseGga(char *line, GpsFix *fix);
#endif
