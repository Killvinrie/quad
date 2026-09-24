#ifndef CONTROL_LINK_H
#define CONTROL_LINK_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* QCTRL v1 is the ESP32-F411 UART frame. The retired nRF24 controller also
 * used this format, so its 20-byte size remains unchanged. */
#define CONTROL_MAGIC0 0x51U /* 'Q' */
#define CONTROL_MAGIC1 0x43U /* 'C' */
#define CONTROL_VERSION 1U
#define CONTROL_FRAME_SIZE 20U
#define CONTROL_BODY_SIZE 18U
#define CONTROL_AXIS_COUNT 4U
#define CONTROL_FLAG_ARM 0x01U

typedef struct {
    uint8_t sequence;
    uint16_t axis[CONTROL_AXIS_COUNT]; /* yaw, throttle, pitch, roll: 0..4095 */
    uint16_t buttons;                   /* bit 0..7: KEY1..KEY8, 1 = pressed */
    uint8_t flags;
    uint16_t battery_mv;
} ControlCommand;

typedef struct {
    uint8_t bytes[CONTROL_FRAME_SIZE];
    size_t used;
} ControlParser;

static inline uint16_t control_crc(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xffffU;
    while (n--) {
        crc ^= (uint16_t)*p++ << 8;
        for (unsigned i = 0; i < 8; ++i)
            crc = (uint16_t)((crc << 1) ^
                             ((crc & 0x8000U) ? 0x1021U : 0));
    }
    return crc;
}

static inline void control_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static inline uint16_t control_get_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline void control_encode(uint8_t frame[CONTROL_FRAME_SIZE],
                                  const ControlCommand *command)
{
    frame[0] = CONTROL_MAGIC0;
    frame[1] = CONTROL_MAGIC1;
    frame[2] = CONTROL_VERSION;
    frame[3] = command->sequence;
    for (unsigned i = 0; i < CONTROL_AXIS_COUNT; ++i)
        control_put_u16(frame + 4U + 2U * i, command->axis[i]);
    control_put_u16(frame + 12, command->buttons);
    frame[14] = command->flags;
    control_put_u16(frame + 15, command->battery_mv);
    frame[17] = 0;
    uint16_t crc = control_crc(frame, CONTROL_BODY_SIZE);
    control_put_u16(frame + CONTROL_BODY_SIZE, crc);
}

static inline int control_decode(const uint8_t frame[CONTROL_FRAME_SIZE],
                                 ControlCommand *command)
{
    if (frame[0] != CONTROL_MAGIC0 || frame[1] != CONTROL_MAGIC1 ||
        frame[2] != CONTROL_VERSION ||
        control_get_u16(frame + CONTROL_BODY_SIZE) !=
            control_crc(frame, CONTROL_BODY_SIZE))
        return 0;
    command->sequence = frame[3];
    for (unsigned i = 0; i < CONTROL_AXIS_COUNT; ++i)
        command->axis[i] = control_get_u16(frame + 4U + 2U * i);
    command->buttons = control_get_u16(frame + 12);
    command->flags = frame[14];
    command->battery_mv = control_get_u16(frame + 15);
    return 1;
}

/* Sliding parser: safe after lost, inserted, or unrelated UART bytes. */
static inline int control_feed(ControlParser *parser, uint8_t byte,
                               uint8_t frame[CONTROL_FRAME_SIZE])
{
    if (parser->used < CONTROL_FRAME_SIZE)
        parser->bytes[parser->used++] = byte;
    while (parser->used &&
           (parser->bytes[0] != CONTROL_MAGIC0 ||
            (parser->used > 1 && parser->bytes[1] != CONTROL_MAGIC1) ||
            (parser->used > 2 && parser->bytes[2] != CONTROL_VERSION))) {
        memmove(parser->bytes, parser->bytes + 1, --parser->used);
    }
    if (parser->used != CONTROL_FRAME_SIZE) return 0;
    if (control_get_u16(parser->bytes + CONTROL_BODY_SIZE) !=
        control_crc(parser->bytes, CONTROL_BODY_SIZE)) {
        memmove(parser->bytes, parser->bytes + 1, --parser->used);
        return 0;
    }
    memcpy(frame, parser->bytes, CONTROL_FRAME_SIZE);
    parser->used = 0;
    return 1;
}

#endif
