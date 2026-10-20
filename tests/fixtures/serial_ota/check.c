#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "bms_serial_ota.h"
typedef uint8_t u8;
#define BMS_DIAG_BUILD_ID 0x12345678u
#define MULTI_BOOT_ADDR_0x20000 0x20000u
#define FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_BEGIN 1
#define FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_END 2
static uint8_t flash[0x60000], ota_is_working;
static uint32_t ota_program_offset, ota_program_bootAddr=0x20000u, ota_firmware_max_size=126976u;
static struct { int init_err; } blc_flashProt;
static uint32_t ticks, active, reboot_count, erases, writes;
static uint32_t ble_writes;
static int otaWrite(void *packet){(void)packet;++ble_writes;return 42;}
static int unlocked, deny_layout, fail_erase, bytes_left=-1, tx_busy;
static uint32_t bms_time_ms(void){return ticks;}
static int app_flash_lock_restore_enabled(void){return !unlocked;}
static int flash_store_cfg_layout_supported(void){return !deny_layout;}
static void app_flash_protection_operation(int event,uint32_t a,uint32_t b){(void)a;(void)b;unlocked=event==1;}
static void flash_read_page(uint32_t a,uint32_t n,uint8_t*p){assert(a+n<=sizeof(flash));memcpy(p,flash+a,n);}
static void flash_erase_sector(uint32_t a){assert(unlocked&&a>=ota_program_offset&&a+4096<=ota_program_offset+126976u);++erases;if(!fail_erase)memset(flash+a,255,4096);}
static void flash_write_page(uint32_t a,uint32_t n,uint8_t*p){
    uint32_t i;assert(unlocked&&a+n<=0x40000u);assert(a/256==(a+n-1)/256);++writes;
    assert((a>=ota_program_offset&&a+n<=ota_program_offset+126976u)||(a==active+8&&n==1));
    for(i=0;i<n;++i){if(bytes_left==0)return;if(bytes_left>0)--bytes_left;flash[a+i]&=p[i];}
}
static int modbus_uart_tx_active(void){return tx_busy;}
static void start_reboot(void){++reboot_count;}
/* PRODUCTION_CODE */
static uint8_t image[126976], req[160], rsp[30];
static uint32_t rsp_size;
static void reset_board(uint32_t base){
    memset(&update,0,sizeof(update));memset(flash,0xA5,sizeof(flash));
    active=base;ota_program_offset=base?0:0x20000;memset(flash+ota_program_offset,0,126976);
    flash[active+8]=0x4b;unlocked=0;ota_is_working=0;fail_erase=0;bytes_left=-1;deny_layout=0;
    ticks=0;erases=writes=reboot_count=0;tx_busy=0;ota_program_bootAddr=0x20000u;
}
static uint32_t crc_raw(const uint8_t*p,uint32_t n){uint32_t c=0xffffffffu,i;uint8_t b;for(i=0;i<n;++i){c^=p[i];for(b=0;b<8;++b)c=(c>>1)^((c&1)?0xedb88320u:0);}return c;}
static void make_image(uint32_t size){
    uint32_t i;for(i=0;i<size;++i)image[i]=(uint8_t)(i*31u);
    write32(image+8,0x544c4e4bu);write32(image+24,size);memcpy(image+128,image_info,20);
    write32(image+size-4,crc_raw(image,size-4));assert(crc_raw(image,size)==0);
}
static uint8_t command(uint8_t cmd,uint32_t session,const uint8_t*p,uint32_t n){
    uint16_t c;req[0]=1;req[1]=0x46;req[2]=cmd;req[3]=0x12;req[4]=0x34;write32(req+5,session);
    if(n)memcpy(req+9,p,n);
    c=mb_crc16(req,n+9);req[n+9]=(uint8_t)c;req[n+10]=(uint8_t)(c>>8);
    assert(bms_serial_ota_on_frame(req,n+11,rsp,&rsp_size)==1&&rsp_size==30);
    assert(mb_crc16(rsp,28)==(uint16_t)(rsp[28]|((uint16_t)rsp[29]<<8)));return rsp[6];
}
static void begin(uint32_t size){uint8_t p[17];p[0]=BMS_PRODUCT_ID;write32(p+1,size);write32(p+5,BMS_DIAG_BUILD_ID);write32(p+9,128);write32(p+13,read32(image+size-4));assert(command(1,42,p,17)==0);assert(ota_is_working&&unlocked);}
static void erase_all(void){uint32_t i;for(i=0;i<31;++i)bms_serial_ota_poll();assert(update.state==BMS_SERIAL_OTA_RECEIVING&&erases==31);}
static uint8_t data(uint32_t offset,uint32_t count){uint8_t p[132];write32(p,offset);memcpy(p+4,image+offset,count);return command(2,42,p,count+4);}
static void receive_all(uint32_t size){uint32_t offset;for(offset=0;offset<size;offset+=128){uint32_t n=size-offset;if(n>128)n=128;assert(data(offset,n)==0);}assert(flash[ota_program_offset+8]==255);}
static void verify_all(void){uint32_t i;assert(command(3,42,0,0)==0);for(i=0;i<1024&&update.state==BMS_SERIAL_OTA_VERIFYING;++i)bms_serial_ota_poll();}
int main(void){
    uint32_t side,cut,before;uint8_t saved;
    for(side=0;side<2;++side){
        reset_board(side?0x20000:0);make_image(sizeof(image));begin(sizeof(image));
        before=ble_writes;assert(app_ble_ota_write(0)==0&&ble_writes==before);
        assert(!bms_serial_ota_flash_open(&before));erase_all();
        assert(data(128,128)==OTA_SEQUENCE);assert(data(0,128)==0);before=writes;
        assert(data(0,128)==0&&writes==before);image[50]^=1;assert(data(0,128)==OTA_SEQUENCE);image[50]^=1;
        receive_all(sizeof(image));verify_all();assert(update.state==BMS_SERIAL_OTA_READY);
        assert(flash[active+8]==0x4b&&flash[ota_program_offset+8]==255);
        before=read32(flash+0x40000);assert(command(4,42,0,0)==0);assert(read32(flash+0x40000)==before);
        assert(flash[active+8]!=0x4b&&flash[ota_program_offset+8]==0x4b);
        ticks+=1000;tx_busy=1;bms_serial_ota_poll();assert(!reboot_count);tx_busy=0;bms_serial_ota_poll();assert(reboot_count==1&&!unlocked);
    }
    reset_board(0);make_image(512);begin(512);erase_all();receive_all(512);flash[ota_program_offset+200]^=1;verify_all();assert(update.state==BMS_SERIAL_OTA_ERROR&&!unlocked&&flash[active+8]==0x4b);
    reset_board(0);make_image(512);begin(512);erase_all();receive_all(512);flash[ota_program_offset+9]^=1;verify_all();assert(update.state==BMS_SERIAL_OTA_ERROR&&flash[active+8]==0x4b);
    reset_board(0);make_image(512);begin(512);fail_erase=1;bms_serial_ota_poll();assert(update.state==BMS_SERIAL_OTA_ERROR&&!unlocked);
    for(cut=0;cut<128;++cut){reset_board(0);make_image(512);begin(512);erase_all();bytes_left=(int)cut;assert(data(0,128)==OTA_FLASH);assert(flash[active+8]==0x4b&&!unlocked);}
    for(side=0;side<2;++side)for(cut=0;cut<6;++cut){
        reset_board(side?0x20000:0);make_image(512);begin(512);erase_all();receive_all(512);verify_all();bytes_left=(int)cut;
        (void)command(4,42,0,0);assert(flash[active+8]==0x4b||flash[ota_program_offset+8]==0x4b);
        if(flash[ota_program_offset+8]==0x4b)assert(crc_raw(flash+ota_program_offset,512)==0);
    }
    reset_board(0);make_image(512);ticks=UINT32_MAX-1000u;begin(512);ticks+=15000u;bms_serial_ota_poll();assert(update.state==BMS_SERIAL_OTA_ERROR&&!unlocked);
    reset_board(0);begin(512);assert(command(5,41,0,0)==OTA_SESSION);assert(command(5,42,0,0)==0&&!unlocked);
    reset_board(0);make_image(512);begin(512);erase_all();assert(command(3,42,0,0)==OTA_SEQUENCE);assert(command(4,42,0,0)==OTA_SEQUENCE);
    saved=req[0];req[0]=0;assert(bms_serial_ota_on_frame(req,11,rsp,&rsp_size)==1&&rsp_size==0);req[0]=saved;
    assert(bms_serial_ota_on_frame(req,2,rsp,&rsp_size)==1&&rsp_size==0);
    reset_board(0);ota_program_bootAddr=0x40000;assert(!bms_serial_ota_flash_open(&before));
    reset_board(0);ota_is_working=1;assert(!bms_serial_ota_flash_open(&before)&&!unlocked&&erases==0);
    reset_board(0);before=ble_writes;assert(app_ble_ota_write(0)==42&&ble_writes==before+1);
    puts("PASS serial OTA fault scenarios");return 0;
}
