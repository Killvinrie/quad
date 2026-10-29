#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define OTA_MAGIC 0x41544f51u
#define OTA_SLOT_SIZE 0x20000u
#define OTA_A 0x08040000u
#define OTA_B 0x08060000u
#define OTA_NONE 0xffffffffu
#define OTA_QUERY 1u
#define OTA_ENTER 2u
#define OTA_BEGIN 3u
#define OTA_DATA 4u
#define OTA_END 5u
#define OTA_REBOOT 6u
#define OTA_REPLY 0x80u
/* All integers are little endian. CRC32 is IEEE/zlib, over preceding bytes. */
typedef struct { uint32_t magic, op, seq, len; uint8_t data[256]; uint32_t crc; } OtaPacket;
typedef struct { uint8_t bytes[sizeof(OtaPacket)]; unsigned used; } OtaParser;
static inline uint32_t ota_crc(const void *p, size_t n) {
    const uint8_t *s=p; uint32_t c=~0u;
    while(n--) { c^=*s++; for(unsigned i=0;i<8;i++) c=(c>>1)^((0u-(c&1u))&0xedb88320u); }
    return ~c;
}
static inline void ota_packet(OtaPacket *p,uint32_t op,uint32_t seq,const void *data,unsigned n) {
    memset(p,0,sizeof(*p)); p->magic=OTA_MAGIC; p->op=op; p->seq=seq; p->len=n;
    if(n) memcpy(p->data,data,n);
    p->crc=ota_crc(p,offsetof(OtaPacket,crc));
}
static inline int ota_feed(OtaParser *s,uint8_t b,OtaPacket *out) {
    s->bytes[s->used++]=b;
    if(s->used>=4) {
        uint32_t magic; memcpy(&magic,s->bytes,4);
        if(magic!=OTA_MAGIC) { memmove(s->bytes,s->bytes+1,--s->used); return 0; }
    }
    if(s->used!=sizeof(*out)) return 0;
    memcpy(out,s->bytes,sizeof(*out));
    if(out->len<=256 && out->crc==ota_crc(out,offsetof(OtaPacket,crc))) {s->used=0;return 1;}
    memmove(s->bytes,s->bytes+1,--s->used); return 0;
}
#endif
