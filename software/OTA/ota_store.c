#include "ota_store.h"
static int meta_valid(const OtaMeta *m) {
    return m->magic==OTA_META_MAGIC && m->active<2 &&
        (m->pending<2 || m->pending==OTA_NONE) &&
        m->crc==ota_crc(m,offsetof(OtaMeta,crc));
}
int OtaStore_Load(OtaMeta *m) {
    const OtaMeta *a=(void*)0x08008000u,*b=(void*)0x0800c000u;
    int va=meta_valid(a),vb=meta_valid(b);
    if(!va&&!vb) return 0;
    *m=(!vb || (va && (int32_t)(a->generation-b->generation)>0))?*a:*b;
    return 1;
}
int OtaStore_Erase(unsigned sector) {
    FLASH_EraseInitTypeDef e={0}; uint32_t bad;
    e.TypeErase=FLASH_TYPEERASE_SECTORS;e.Sector=sector;e.NbSectors=1;e.VoltageRange=FLASH_VOLTAGE_RANGE_3;
    IWDG->KR=0xAAAA; HAL_FLASH_Unlock();
    int ok=HAL_FLASHEx_Erase(&e,&bad)==HAL_OK;HAL_FLASH_Lock();IWDG->KR=0xAAAA;return ok;
}
int OtaStore_Write(uint32_t address,const void *data,unsigned n) {
    const uint8_t *s=data; int ok=1; HAL_FLASH_Unlock();
    for(unsigned i=0;i<n;i++) if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,address+i,s[i])!=HAL_OK){ok=0;break;}
    HAL_FLASH_Lock();return ok && memcmp((void*)address,data,n)==0;
}
int OtaStore_Save(OtaMeta *m) {
    OtaMeta old; int have=OtaStore_Load(&old);
    uint32_t dest=0x08008000u;
    if(have) {
        const OtaMeta *a=(void*)dest;
        if(meta_valid(a)&&a->generation==old.generation) dest=0x0800c000u;
    }
    m->magic=OTA_META_MAGIC;m->generation=have?old.generation+1:1;
    m->crc=ota_crc(m,offsetof(OtaMeta,crc));
    uint32_t irq=__get_PRIMASK();__disable_irq();
    int ok=OtaStore_Erase(dest==0x08008000u?2:3)&&OtaStore_Write(dest,m,sizeof(*m));
    __set_PRIMASK(irq);return ok;
}
int OtaStore_Valid(const OtaMeta *m,unsigned slot) {
    if(slot>1 || m->size[slot]<8 || m->size[slot]>OTA_SLOT_SIZE) return 0;
    uint32_t base=OtaStore_Base(slot),sp=*(uint32_t*)base,pc=*(uint32_t*)(base+4);
    return sp>0x20000000u && sp<=0x20020000u && !(sp&7) && (pc&1) &&
        pc>=base && pc<base+m->size[slot] && ota_crc((void*)base,m->size[slot])==m->hash[slot];
}

unsigned OtaStore_Select(OtaMeta *m) {
    if(m->pending<2) {
        if(!m->tried && OtaStore_Valid(m,m->pending)) {
            m->tried=1;
            return OtaStore_Save(m)?m->pending:OTA_NONE;
        }
        m->pending=OTA_NONE;m->tried=0;
        if(!OtaStore_Save(m)) return OTA_NONE;
    }
    return OtaStore_Valid(m,m->active)?m->active:OTA_NONE;
}
