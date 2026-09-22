#ifndef DISPLAY_LINK_H
#define DISPLAY_LINK_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Version 1: QD, version, 8 rows of 21 ASCII characters, CRC16-CCITT LE.
 * No structs are sent over the wire; no padding/endian dependencies. */
#define DISPLAY_ROWS 8
#define DISPLAY_COLS 21
#define DISPLAY_FRAME_SIZE (3 + DISPLAY_ROWS * DISPLAY_COLS + 2)
typedef struct { uint8_t bytes[DISPLAY_FRAME_SIZE]; size_t used; } DisplayParser;

static inline uint16_t display_crc(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xffff;
    while (n--) {
        crc ^= (uint16_t)*p++ << 8;
        for (unsigned i = 0; i < 8; ++i)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}
static inline void display_encode(uint8_t *frame,
                                  char rows[DISPLAY_ROWS][DISPLAY_COLS + 1])
{
    frame[0] = 'Q'; frame[1] = 'D'; frame[2] = 1;
    for (unsigned r = 0; r < DISPLAY_ROWS; ++r) {
        size_t n = strlen(rows[r]);
        if (n > DISPLAY_COLS) n = DISPLAY_COLS;
        memset(frame + 3 + r * DISPLAY_COLS, ' ', DISPLAY_COLS);
        memcpy(frame + 3 + r * DISPLAY_COLS, rows[r], n);
    }
    uint16_t crc = display_crc(frame, DISPLAY_FRAME_SIZE - 2);
    frame[DISPLAY_FRAME_SIZE - 2] = (uint8_t)crc;
    frame[DISPLAY_FRAME_SIZE - 1] = (uint8_t)(crc >> 8);
}
/* Sliding resynchronization also recovers after inserted/lost bytes. */
static inline int display_feed(DisplayParser *p, uint8_t byte,
                               char rows[DISPLAY_ROWS][DISPLAY_COLS + 1])
{
    p->bytes[p->used++] = byte;
    while (p->used && (p->bytes[0] != 'Q' ||
           (p->used > 1 && p->bytes[1] != 'D') ||
           (p->used > 2 && p->bytes[2] != 1))) {
        memmove(p->bytes, p->bytes + 1, --p->used);
    }
    if (p->used != DISPLAY_FRAME_SIZE) return 0;
    uint16_t crc = display_crc(p->bytes, DISPLAY_FRAME_SIZE - 2);
    if (p->bytes[DISPLAY_FRAME_SIZE - 2] != (uint8_t)crc ||
        p->bytes[DISPLAY_FRAME_SIZE - 1] != (uint8_t)(crc >> 8)) {
        memmove(p->bytes, p->bytes + 1, --p->used);
        return 0;
    }
    for (unsigned r = 0; r < DISPLAY_ROWS; ++r) {
        memcpy(rows[r], p->bytes + 3 + r * DISPLAY_COLS, DISPLAY_COLS);
        rows[r][DISPLAY_COLS] = 0;
    }
    p->used = 0;
    return 1;
}
#endif
