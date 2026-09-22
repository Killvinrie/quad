#include "gps_nmea.h"
#include <string.h>
#include <stdlib.h>

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static int coordinate(const char *s, unsigned digits, unsigned max_degrees)
{
    size_t n = strlen(s);
    if (n < digits || n >= 16) return 0;
    for (size_t i = 0; i < n; ++i)
        if (!(s[i] >= '0' && s[i] <= '9') && !(i == digits && s[i] == '.'))
            return 0;
    unsigned degrees = 0;
    for (unsigned i = 0; i < digits - 2; ++i) degrees = degrees * 10 + (unsigned)(s[i] - '0');
    double minutes = strtod(s + digits - 2, NULL);
    return minutes < 60 && degrees <= max_degrees &&
           (degrees < max_degrees || minutes == 0);
}
int Gps_ParseGga(char *line, GpsFix *fix)
{
    if (!line || !fix || line[0] != '$') return 0;
    char *star = strchr(line, '*');
    if (!star || strlen(star) < 3 || hex(star[1]) < 0 || hex(star[2]) < 0)
        return 0;
    if (star[3] && strcmp(star + 3, "\r") && strcmp(star + 3, "\n") &&
        strcmp(star + 3, "\r\n")) return 0;
    uint8_t sum = 0;
    for (char *p = line + 1; p < star; ++p) sum ^= (uint8_t)*p;
    if (sum != (uint8_t)((hex(star[1]) << 4) | hex(star[2]))) return 0;
    *star = 0;
    char *fields[16]; unsigned count = 1;
    fields[0] = line + 1;
    for (char *p = line + 1; *p; ++p) {
        if (*p == ',') {
            *p = 0;
            if (count == 16) return 0;
            fields[count++] = p + 1;
        }
    }
    if (count < 10 || strlen(fields[0]) != 5 ||
        strcmp(fields[0] + 2, "GGA")) return 0;
    if (strlen(fields[6]) != 1 || fields[6][0] < '0' || fields[6][0] > '8')
        return 0;
    GpsFix next = {0};
    next.quality = (uint8_t)(fields[6][0] - '0');
    if (strlen(fields[7]) > 2) return 0;
    for (char *p = fields[7]; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
        next.satellites = (uint8_t)(next.satellites * 10 + *p - '0');
    }
    if (next.quality) {
        if (!coordinate(fields[2], 4, 90) || !coordinate(fields[4], 5, 180) ||
            strlen(fields[3]) != 1 || strlen(fields[5]) != 1 ||
            !strchr("NS", fields[3][0]) || !strchr("EW", fields[5][0])) return 0;
        strcpy(next.latitude, fields[2]); strcpy(next.longitude, fields[4]);
        next.ns = fields[3][0]; next.ew = fields[5][0];
    }
    *fix = next;
    return 1;
}
