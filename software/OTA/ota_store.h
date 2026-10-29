#ifndef OTA_STORE_H
#define OTA_STORE_H
#include "stm32f4xx_hal.h"
#include "ota_protocol.h"
#define OTA_META_MAGIC 0x4d415451u
/* Separate erase sectors 2 and 3; CRC programmed last commits each record. */
typedef struct {
    uint32_t magic,generation,active,pending,tried;
    uint32_t size[2],hash[2],version[2],crc;
} OtaMeta;
unsigned OtaStore_Select(OtaMeta *m);
int OtaStore_Load(OtaMeta *m);
int OtaStore_Save(OtaMeta *m);
int OtaStore_Valid(const OtaMeta *m,unsigned slot);
int OtaStore_Erase(unsigned sector);
int OtaStore_Write(uint32_t address,const void *data,unsigned n);
static inline uint32_t OtaStore_Base(unsigned slot) {return slot?OTA_B:OTA_A;}
#endif
