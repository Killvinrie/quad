#include "ota_store.h"
static UART_HandleTypeDef uart;
void SysTick_Handler(void){HAL_IncTick();}
void Error_Handler(void){while(1){IWDG->KR=0xAAAA;}}
static void jump(unsigned slot) {
    uint32_t base=OtaStore_Base(slot),sp=*(uint32_t*)base,pc=*(uint32_t*)(base+4);
    while(!(USART2->SR&USART_SR_TC)){}
    HAL_UART_DeInit(&uart);HAL_RCC_DeInit();HAL_DeInit();__disable_irq();
    SysTick->CTRL=0;SysTick->LOAD=0;SysTick->VAL=0;
    for(unsigned i=0;i<8;i++){NVIC->ICER[i]=~0u;NVIC->ICPR[i]=~0u;}
    SCB->ICSR=SCB_ICSR_PENDSTCLR_Msk|SCB_ICSR_PENDSVCLR_Msk;
    SCB->VTOR=base;__DSB();__ISB();
    __asm volatile("msr msp, %0\n movs r0, #0\n msr control, r0\n cpsie i\n bx %1"::"r"(sp),"r"(pc):"r0","memory");
    __builtin_unreachable();
}
int main(void) {
    HAL_Init();
    IWDG->KR=0xCCCC;IWDG->KR=0x5555;IWDG->PR=6;IWDG->RLR=2000;
    while(IWDG->SR){} IWDG->KR=0xAAAA;
    __HAL_RCC_PWR_CLK_ENABLE();HAL_PWR_EnableBkUpAccess();
    int requested=RTC->BKP0R==OTA_MAGIC;RTC->BKP0R=0;
    __HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_2|GPIO_PIN_3;g.Mode=GPIO_MODE_AF_PP;g.Alternate=GPIO_AF7_USART2;g.Speed=GPIO_SPEED_FREQ_HIGH;HAL_GPIO_Init(GPIOA,&g);
    uart.Instance=USART2;uart.Init.BaudRate=115200;uart.Init.WordLength=UART_WORDLENGTH_8B;uart.Init.StopBits=UART_STOPBITS_1;uart.Init.Parity=UART_PARITY_NONE;uart.Init.Mode=UART_MODE_TX_RX;uart.Init.OverSampling=UART_OVERSAMPLING_16;
    if(HAL_UART_Init(&uart)!=HAL_OK) Error_Handler();
    OtaMeta m;int have=OtaStore_Load(&m);
    unsigned choice=have?OtaStore_Select(&m):OTA_NONE;
    if(choice<2 && (m.pending==choice || !requested)) jump(choice);
    OtaParser parser={0};OtaPacket p,last_reply;int cached=0;uint32_t last_request_crc=0;
    uint32_t target=OTA_NONE,size=0,hash=0,version=0,offset=0,last=HAL_GetTick();
    while(1) {
        IWDG->KR=0xAAAA;
        if(target==OTA_NONE && have && HAL_GetTick()-last>30000 && OtaStore_Valid(&m,m.active)) jump(m.active);
        uint8_t b;if(HAL_UART_Receive(&uart,&b,1,10)!=HAL_OK) continue;
        if(!ota_feed(&parser,b,&p)) continue;
        last=HAL_GetTick();
        if(cached && p.crc==last_request_crc && p.seq==last_reply.seq && (p.op|OTA_REPLY)==last_reply.op) {
            HAL_UART_Transmit(&uart,(void*)&last_reply,sizeof(last_reply),100);continue;
        }
        uint32_t out[5]={0,1,have?m.active:OTA_NONE,have?m.version[m.active]:0,have?m.pending:OTA_NONE};
        if(p.op==OTA_BEGIN && p.len==16 && have) {
            uint32_t args[4];memcpy(args,p.data,16);
            if(args[0]>1 || args[0]==m.active || args[1]<8 || args[1]>OTA_SLOT_SIZE || args[3]<=m.version[m.active]) out[0]=3;
            else if(!OtaStore_Erase(6+args[0])) out[0]=4;
            else {target=args[0];size=args[1];hash=args[2];version=args[3];offset=0;}
        } else if(p.op==OTA_DATA && target<2 && p.len>4) {
            uint32_t at;memcpy(&at,p.data,4);unsigned n=p.len-4;
            if(at!=offset || offset>size || n>size-offset) out[0]=5;
            else if(!OtaStore_Write(OtaStore_Base(target)+offset,p.data+4,n)) out[0]=4;
            else offset+=n;
        } else if(p.op==OTA_END && target<2 && offset==size) {
            m.size[target]=size;m.hash[target]=hash;m.version[target]=version;
            if(!OtaStore_Valid(&m,target)) out[0]=6;
            else {m.pending=target;m.tried=0;if(!OtaStore_Save(&m))out[0]=4;else target=OTA_NONE;}
        } else if(p.op!=OTA_QUERY && p.op!=OTA_REBOOT) out[0]=7;
        ota_packet(&last_reply,p.op|OTA_REPLY,p.seq,out,sizeof(out));cached=1;last_request_crc=p.crc;
        HAL_UART_Transmit(&uart,(void*)&last_reply,sizeof(last_reply),100);
        if(p.op==OTA_REBOOT && !out[0]) NVIC_SystemReset();
    }
}
