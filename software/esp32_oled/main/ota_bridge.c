#include "ota_bridge.h"
#include "ota_protocol.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "psa/crypto.h"
#include <stdlib.h>
#include <stdatomic.h>
#if defined(CONFIG_QUAD_OTA_ENABLE) && CONFIG_QUAD_LINK_BAUD != 115200
#error "STM32 Bootloader OTA requires 115200 baud"
#endif
static atomic_bool busy;
static QueueHandle_t replies;
static OtaParser parser;
static uint32_t sequence;
static const char *TAG="ota_bridge";
bool OtaBridge_Busy(void){return atomic_load(&busy);}
void OtaBridge_Rx(uint8_t byte) {
    OtaPacket p;
    if(replies && ota_feed(&parser,byte,&p) && (p.op&OTA_REPLY)) xQueueSend(replies,&p,0);
}
static int rpc(unsigned op,const void *data,unsigned len,uint32_t out[5]) {
    OtaPacket p,r;ota_packet(&p,op,++sequence,data,len);
    for(unsigned attempt=0;attempt<4;attempt++) {
        uart_write_bytes(UART_NUM_1,&p,sizeof(p));
        TickType_t start=xTaskGetTickCount();
        while(xTaskGetTickCount()-start<pdMS_TO_TICKS(4000)) {
            if(xQueueReceive(replies,&r,pdMS_TO_TICKS(100)) && r.seq==p.seq && r.op==(op|OTA_REPLY) && r.len==20) {
                memcpy(out,r.data,20);
                if(out[0])ESP_LOGW(TAG,"STM32 rejected op=%u, status=%lu",op,(unsigned long)out[0]);
                return out[0]==0;
            }
        }
        ESP_LOGW(TAG,"STM32 reply timeout: op=%u, attempt=%u/4",op,attempt+1);
    }
    ESP_LOGW(TAG,"STM32 UART timeout: op=%u, no reply after 4 attempts",op);
    return 0;
}
/* Exact length, status 200 only. Never follow an arbitrary redirect. */
static int fetch(const char *url,uint8_t *dest,int expected,int capacity) {
    esp_http_client_config_t c={.url=url,.timeout_ms=10000,.disable_auto_redirect=true};
    esp_http_client_handle_t h=esp_http_client_init(&c);if(!h)return -1;
    int total=0,result=-1;
    esp_err_t err=esp_http_client_open(h,0);
    if(err!=ESP_OK){ESP_LOGW(TAG,"HTTP connection failed: %s (%s)",url,esp_err_to_name(err));goto done;}
    int64_t declared=esp_http_client_fetch_headers(h);
    int status=esp_http_client_get_status_code(h);
    if(status!=200 || declared>capacity || (expected>0 && declared!=expected)) {
        ESP_LOGW(TAG,"HTTP rejected: %s status=%d length=%lld",url,status,(long long)declared);goto done;
    }
    while(total<capacity) {
        int n=esp_http_client_read(h,(char*)dest+total,capacity-total);
        if(n<0)goto done;
        if(n==0)break;
        total+=n;
    }
    if(!esp_http_client_is_complete_data_received(h))goto done;
    if(expected>0 && total!=expected)goto done;
    result=total;
done:
    if(result<0)ESP_LOGW(TAG,"HTTP download incomplete/invalid: %s (%d bytes)",url,total);
    esp_http_client_close(h);esp_http_client_cleanup(h);return result;
}
static const cJSON *field(const cJSON *j,const char *name){return cJSON_GetObjectItemCaseSensitive(j,name);}
static int number(const cJSON *j,const char *key,uint32_t *out) {
    const cJSON *v=field(j,key);
    if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>4294967295.0)return 0;
    *out=(uint32_t)v->valuedouble;return v->valuedouble==*out;
}
static int hash_ok(const uint8_t *image,unsigned n,const char *hex) {
    if(!hex || strlen(hex)!=64)return 0;
    unsigned char hash[32];size_t hash_len;
    if(psa_crypto_init()!=PSA_SUCCESS || psa_hash_compute(PSA_ALG_SHA_256,image,n,hash,sizeof(hash),&hash_len)!=PSA_SUCCESS || hash_len!=32)return 0;
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++)if(hex[i*2]!=digits[hash[i]>>4]||hex[i*2+1]!=digits[hash[i]&15])return 0;
    return 1;
}
static void update(void) {
    char text[4096],url[256];uint32_t info[5],version=0,size=0;
    ESP_LOGI(TAG,"Checking Raspberry Pi: %s",CONFIG_QUAD_OTA_MANIFEST);
    int n=fetch(CONFIG_QUAD_OTA_MANIFEST,(void*)text,0,sizeof(text)-1);
    if(n<=0)return;
    text[n]=0;cJSON *j=cJSON_Parse(text);uint8_t *image=NULL;
    const cJSON *target=field(j,"target");
    if(!j || !cJSON_IsString(target) || strcmp(target->valuestring,"stm32f411ce") || !number(j,"version",&version)) {
        ESP_LOGW(TAG,"HTTP 200 but manifest target/version is invalid");goto done;
    }
    ESP_LOGI(TAG,"Raspberry Pi communication OK: HTTP 200, version=%lu",(unsigned long)version);
    /* Query does not interrupt control; STM32 rejects requests while armed. */
    if(!rpc(OTA_QUERY,NULL,0,info))goto done;
    ESP_LOGI(TAG,"STM32: mode=%s active=%lu confirmed_version=%lu pending=%lu",
             info[1]?"boot":"app",(unsigned long)info[2],(unsigned long)info[3],(unsigned long)info[4]);
    if(info[2]>1 || info[4]!=OTA_NONE) {ESP_LOGW(TAG,"STM32 bank invalid or pending confirmation; skipping");goto done;}
    if(version<=info[3]) {ESP_LOGI(TAG,"No newer firmware; Raspberry Pi and STM32 communication both OK");goto done;}
    unsigned slot=1-info[2];
    const cJSON *banks=field(j,"banks"),*bank=field(banks,slot?"B":"A");
    const cJSON *path=field(bank,"file"),*sha=field(bank,"sha256");
    if(!number(bank,"size",&size)||size<8||size>OTA_SLOT_SIZE||!cJSON_IsString(path)||!cJSON_IsString(sha))goto done;
    /* Files must be siblings of manifest; reject path traversal and remote URLs. */
    if(strspn(path->valuestring,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-. ")!=strlen(path->valuestring)||strstr(path->valuestring,".."))goto done;
    const char *slash=strrchr(CONFIG_QUAD_OTA_MANIFEST,'/');if(!slash)goto done;
    int prefix=(int)(slash-CONFIG_QUAD_OTA_MANIFEST)+1;
    if(snprintf(url,sizeof(url),"%.*s%s",prefix,CONFIG_QUAD_OTA_MANIFEST,path->valuestring)>=(int)sizeof(url))goto done;
    image=malloc(size);if(!image)goto done;
    ESP_LOGI(TAG,"Downloading version %lu bank %c: %lu bytes",(unsigned long)version,slot?'B':'A',(unsigned long)size);
    TickType_t download_start=xTaskGetTickCount();
    if(fetch(url,image,size,size)!=(int)size || !hash_ok(image,size,sha->valuestring))goto done;
    ESP_LOGI(TAG,"Download and SHA256 check completed in %lu ms",
             (unsigned long)((xTaskGetTickCount()-download_start)*portTICK_PERIOD_MS));
    uint32_t args[4]={slot,size,ota_crc(image,size),version};
    ESP_LOGI(TAG,"Downloaded version %lu bank %c; requesting boot",(unsigned long)version,slot?'B':'A');
    if(!rpc(OTA_QUERY,NULL,0,info)||info[2]==slot||info[4]!=OTA_NONE)goto done;
    if(info[1]==0) {
        if(!rpc(OTA_ENTER,NULL,0,info))goto done;
        atomic_store(&busy,true);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    atomic_store(&busy,true);
    if(!rpc(OTA_QUERY,NULL,0,info)||info[1]!=1||info[2]==slot)goto done;
    ESP_LOGI(TAG,"Bootloader ready; erasing bank %c",slot?'B':'A');
    if(!rpc(OTA_BEGIN,args,sizeof(args),info))goto done;
    ESP_LOGI(TAG,"Erase complete; writing STM32: 0%%");
    TickType_t write_start=xTaskGetTickCount(),last_progress=write_start;
    for(uint32_t at=0;at<size;) {
        uint8_t block[256];unsigned count=size-at;if(count>252)count=252;
        memcpy(block,&at,4);memcpy(block+4,image+at,count);
        if(!rpc(OTA_DATA,block,count+4,info))goto done;
        at+=count;
        TickType_t now=xTaskGetTickCount();
        if(at==size || now-last_progress>=pdMS_TO_TICKS(1000)) {
            ESP_LOGI(TAG,"Writing STM32: %lu%% (%lu/%lu bytes)",
                     (unsigned long)(at*100u/size),(unsigned long)at,(unsigned long)size);
            last_progress=now;
        }
    }
    ESP_LOGI(TAG,"STM32 transfer completed in %lu ms; verifying image",
             (unsigned long)((xTaskGetTickCount()-write_start)*portTICK_PERIOD_MS));
    if(!rpc(OTA_END,NULL,0,info))goto done;
    OtaPacket reboot;ota_packet(&reboot,OTA_REBOOT,++sequence,NULL,0);
    uart_write_bytes(UART_NUM_1,&reboot,sizeof(reboot));
    ESP_LOGI(TAG,"Image verified; rebooting STM32 and waiting 10 s for application confirmation");
    vTaskDelay(pdMS_TO_TICKS(10000));
    if(rpc(OTA_QUERY,NULL,0,info)&&info[1]==0&&info[3]==version&&info[4]==OTA_NONE)
        ESP_LOGI(TAG,"Version %lu confirmed",(unsigned long)version);
    else ESP_LOGE(TAG,"New version not confirmed; inspect STM32 / rollback status");
done:
    free(image);cJSON_Delete(j);atomic_store(&busy,false);
}
static void task(void *arg) {
    (void)arg;
    unsigned waiting=0;
    for(;;) {
        esp_netif_ip_info_t ip;
        esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if(sta && esp_netif_get_ip_info(sta,&ip)==ESP_OK && ip.ip.addr) {
            waiting=0;
            update();
            vTaskDelay(pdMS_TO_TICKS(CONFIG_QUAD_OTA_POLL_SECONDS*1000u));
        } else {
            if(waiting++%5u==0)ESP_LOGI(TAG,"Waiting for router connection and DHCP address");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
void OtaBridge_Start(void) {
#ifdef CONFIG_QUAD_OTA_ENABLE
    ESP_LOGI(TAG,"OTA enabled; check interval: %d s; manifest: %s",
             CONFIG_QUAD_OTA_POLL_SECONDS,CONFIG_QUAD_OTA_MANIFEST);
    replies=xQueueCreate(8,sizeof(OtaPacket));
    if(!replies || xTaskCreate(task,"ota_bridge",8192,NULL,3,NULL)!=pdPASS) ESP_LOGE(TAG,"Cannot start OTA worker");
#else
    ESP_LOGW(TAG,"OTA disabled: enable Quad STM32 A/B OTA in menuconfig");
#endif
}
