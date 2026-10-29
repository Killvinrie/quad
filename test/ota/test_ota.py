"""Host verification of actual C parser/store under torn flash writes."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('release', ROOT/'tools/ota_release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

class OtaTests(unittest.TestCase):
    def test_release(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            for slot, base in release.SLOTS.items():
                (d/slot).write_bytes(struct.pack('<II', 0x20020000, base+9)+b'abcdef')
            (d/'boot').write_bytes(b'bootboot')
            m = release.publish(7, d/'A', d/'B', d/'boot', d/'out')
            self.assertEqual(m['version'], 7)
            for slot in 'AB':
                data = (d/'out'/m['banks'][slot]['file']).read_bytes()
                release.check_image(data, release.SLOTS[slot])
            meta = release.metadata((d/'A').read_bytes(), 7)
            self.assertEqual(len(meta), 48)
            self.assertEqual(zlib.crc32(meta[:-4]), struct.unpack_from('<I', meta, 44)[0])
            for line in (d/'out/factory.hex').read_text().splitlines():
                self.assertEqual(sum(bytes.fromhex(line[1:])) & 255, 0)
            with self.assertRaises(ValueError):
                release.check_image((d/'A').read_bytes(), release.SLOTS['B'])

    def test_c_flash_and_protocol(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d/'stm32f4xx_hal.h').write_text(r'''
#include <stdint.h>
typedef struct {uint32_t KR;} WD;
extern WD wd;
#define IWDG (&wd)
typedef struct {unsigned TypeErase,Sector,NbSectors,VoltageRange;} FLASH_EraseInitTypeDef;
#define FLASH_TYPEERASE_SECTORS 0
#define FLASH_VOLTAGE_RANGE_3 0
#define FLASH_TYPEPROGRAM_BYTE 0
#define HAL_OK 0
unsigned __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(unsigned x);
void HAL_FLASH_Unlock(void);
void HAL_FLASH_Lock(void);
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef*,uint32_t*);
int HAL_FLASH_Program(unsigned,uint32_t,uint64_t);
''')
            (d/'main.c').write_text(r'''
#include <assert.h>
#include <sys/mman.h>
#include "ota_store.h"
WD wd;
static int cut=-1;
unsigned __get_PRIMASK(void){return 0;}
void __disable_irq(void){}
void __set_PRIMASK(unsigned x){(void)x;}
void HAL_FLASH_Unlock(void){}
void HAL_FLASH_Lock(void){}
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e,uint32_t *bad){
    (void)bad;assert(e->Sector==2||e->Sector==3);
    memset((void*)(uintptr_t)(0x08000000+e->Sector*0x4000),255,0x4000);return 0;
}
int HAL_FLASH_Program(unsigned type,uint32_t addr,uint64_t value){
    (void)type;if(cut==0)return 1;if(cut>0)--cut;
    uint8_t *p=(void*)(uintptr_t)addr;assert((*p&(uint8_t)value)==(uint8_t)value);*p=value;return 0;
}
int main(void){
    void *flash=mmap((void*)0x08000000,0x80000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    assert(flash==(void*)0x08000000);memset(flash,255,0x80000);
    assert(ota_crc("123456789",9)==0xcbf43926);
    OtaPacket p,q;OtaParser parser={0};uint32_t payload=42;
    ota_packet(&p,OTA_QUERY,19,&payload,4);
    for(int i=0;i<13;i++)assert(!ota_feed(&parser,0xa5,&q));
    for(unsigned i=0;i<sizeof(p);i++)assert(ota_feed(&parser,((uint8_t*)&p)[i],&q)==(i==sizeof(p)-1));
    assert(q.seq==19);p.data[0]^=1;
    for(unsigned i=0;i<sizeof(p);i++)assert(!ota_feed(&parser,((uint8_t*)&p)[i],&q));
    ota_packet(&p,OTA_DATA,20,&payload,4);int found=0;
    for(unsigned i=0;i<sizeof(p);i++)found+=ota_feed(&parser,((uint8_t*)&p)[i],&q);
    assert(found==1&&q.seq==20);
    OtaMeta m={0},got;assert(!OtaStore_Load(&got));m.active=0;m.pending=OTA_NONE;m.version[0]=1;
    assert(OtaStore_Save(&m));assert(OtaStore_Load(&got)&&got.version[0]==1);
    /* Every interrupted byte prefix must preserve the previous committed record. */
    for(int n=0;n<48;n++){
        cut=n;OtaMeta next=m;next.version[0]=2;assert(!OtaStore_Save(&next));
        assert(OtaStore_Load(&got)&&got.version[0]==1);
    }
    cut=-1;m.version[0]=2;assert(OtaStore_Save(&m));assert(OtaStore_Load(&got)&&got.version[0]==2);
    for(int n=0;n<48;n++){
        cut=n;OtaMeta next=m;next.version[0]=3;assert(!OtaStore_Save(&next));
        assert(OtaStore_Load(&got)&&got.version[0]==2);
    }
    cut=-1;
    uint32_t *vectors=(void*)OTA_A;vectors[0]=0x20020000;vectors[1]=OTA_A+9;
    m.size[0]=16;m.hash[0]=ota_crc(vectors,16);assert(OtaStore_Valid(&m,0));
    vectors[1]=OTA_B+9;assert(!OtaStore_Valid(&m,0));
    vectors[1]=OTA_A+9;
    uint32_t *vb=(void*)OTA_B;vb[0]=0x20020000;vb[1]=OTA_B+9;
    m.size[1]=16;m.hash[1]=ota_crc(vb,16);m.pending=1;m.tried=0;
    assert(OtaStore_Save(&m));assert(OtaStore_Select(&m)==1);
    assert(OtaStore_Load(&m)&&m.tried==1&&m.active==0);
    /* Reset before application confirmation rolls back to A. */
    assert(OtaStore_Select(&m)==0);assert(m.pending==OTA_NONE);
    m.pending=1;m.tried=0;assert(OtaStore_Save(&m));
    assert(OtaStore_Select(&m)==1);
    /* Application confirmation persists B as the active bank. */
    m.active=1;m.pending=OTA_NONE;m.tried=0;assert(OtaStore_Save(&m));
    assert(OtaStore_Load(&m)&&OtaStore_Select(&m)==1);
    vb[1]=OTA_A+9;assert(OtaStore_Select(&m)==OTA_NONE);
    return 0;
}
''')
            subprocess.run(['gcc', '-std=gnu99', '-Wno-int-to-pointer-cast', '-I'+str(d),
                            '-I'+str(ROOT/'software/OTA'), '-I'+str(ROOT/'software/Common'),
                            str(d/'main.c'), str(ROOT/'software/OTA/ota_store.c'), '-o', str(d/'check')], check=True)
            subprocess.run([str(d/'check')], check=True)

if __name__ == '__main__':
    unittest.main()
