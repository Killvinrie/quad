#include "ota_app.h"
#include "ota_store.h"
#include "motor_app.h"
static OtaParser parser;
static volatile uint8_t ready;
static OtaPacket request;
void OtaApp_RxByte(uint8_t b) {
    OtaPacket p;
    if(ota_feed(&parser,b,&p) && !ready && (p.op==OTA_QUERY || p.op==OTA_ENTER)) {request=p;ready=1;}
}
void OtaApp_Poll(void) {
    static int confirmed;
    OtaMeta m={0};
    if(!confirmed && HAL_GetTick()>5000 && OtaStore_Load(&m)) {
        unsigned running=SCB->VTOR==OTA_B?1:0;
        if(m.pending==running && m.tried && OtaStore_Valid(&m,running)) {
            m.active=running;m.pending=OTA_NONE;m.tried=0;
            if(!OtaStore_Save(&m)) return;
        }
        confirmed=1;
    }
    if(!ready) return;
    OtaPacket p; uint32_t irq=__get_PRIMASK();__disable_irq();p=request;ready=0;__set_PRIMASK(irq);
    MotorAppStatus motor;MotorApp_GetStatus(&motor);
    uint32_t status[5]={0,0,0,0,0};
    if(!OtaStore_Load(&m)) status[0]=1;
    else {status[2]=m.active;status[3]=m.version[m.active];status[4]=m.pending;}
    if(motor.armed) status[0]=2;
    if(p.op==OTA_ENTER && (motor.armed || !confirmed || m.pending!=OTA_NONE)) status[0]=2;
    OtaPacket reply;ota_packet(&reply,p.op|OTA_REPLY,p.seq,status,sizeof(status));
    /* Main loop owns TX; polling avoids interleaving display and OTA replies. */
    for(unsigned i=0;i<sizeof(reply);i++) {while(!(USART2->SR&USART_SR_TXE)){} USART2->DR=((uint8_t*)&reply)[i];}
    while(!(USART2->SR&USART_SR_TC)){}
    if(p.op==OTA_ENTER && !status[0]) {
        __HAL_RCC_PWR_CLK_ENABLE();HAL_PWR_EnableBkUpAccess();
        RTC->BKP0R=OTA_MAGIC;__DSB();NVIC_SystemReset();
    }
}
