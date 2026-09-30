#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#define CLOCK_SYS_CLOCK_HZ 1000000u
#define BMS_AFE_HW_REQUESTED_REG_BASE 0x2500u
#define BMS_AFE_HW_PROFILE_WORD_COUNT 35u
#define MB_EX_ILLEGAL_VALUE 3u
static u32 now;
static u32 clock_time(void){return now;}
static int clock_time_exceed(u32 start,u32 us){return (u32)(now-start)>us;}
static u16 bms_afe_hw_profile_expected_model(void){return 0x1124;}
/* HEADER */
/* ACCESS */
#define u16be access_u16be
#define mb_crc16 access_crc16
static int writes, apply_error;
static u8 afe_hw_profile_write_block(const u8*p,u16 qty){
    assert(qty==35 && p[0]==0 && p[1]==1); ++writes;
    if(!apply_error)bms_afe_hw_access_close();
    return (u8)apply_error;
}
/* GATE */
static u8 frame[79], reply[20]; static u32 reply_len;
static int send(u8*packet,u32 len){
    u16 crc=access_crc16(packet,len-2);packet[len-2]=(u8)crc;packet[len-1]=(u8)(crc>>8);
    return bms_afe_hw_access_modbus_on_frame(packet,len,reply,&reply_len);
}
static u16 open_session(void){
    u8 q[]={1,0x42,1,0x41,0x46,0x45,0x48,0,0};assert(send(q,sizeof q));
    assert(reply_len==13 && reply[3]==0 && reply[8]==2);return access_u16be(reply+4);
}
static u8 chunk(u16 token,u8 offset,u8 count){
    u8 q[20]={1,0x42,5};q[3]=(u8)(token>>8);q[4]=(u8)token;q[5]=offset;q[6]=count;
    assert(count<=11);memcpy(q+7,frame+offset,count);assert(send(q,9u+count));
    if(!reply[3])assert(reply_len==9 && access_u16be(reply+4)==token && reply[6]==offset+count);
    return reply[3];
}
static u8 commit(u16 token){
    u8 q[]={1,0x42,6,(u8)(token>>8),(u8)token,0,0};assert(send(q,sizeof q));
    assert(reply_len==6);return reply[3];
}
static void stage(u16 token){for(u8 i=0;i<79;i+=11)assert(!chunk(token,i,(79-i)>11?11:(79-i)));}
static void inner_crc(void){u16 c=access_crc16(frame,77);frame[77]=(u8)c;frame[78]=(u8)(c>>8);}
int main(void){
    frame[0]=1;frame[1]=0x10;frame[2]=0x25;frame[5]=35;frame[6]=70;frame[8]=1;inner_crc();
    u16 token=open_session();stage(token);assert(writes==0);assert(!commit(token));assert(writes==1);
    assert(commit(token)!=0 && writes==1);
    token=open_session();assert(!chunk(token,0,11));assert(commit(token)!=0 && writes==1);
    token=open_session();assert(chunk(token,11,11)!=0);assert(commit(token)!=0);
    token=open_session();assert(!chunk(token,0,11));assert(chunk(token,0,11)!=0);assert(commit(token)!=0);
    token=open_session();assert(chunk(token+1,0,11)!=0);assert(commit(token)!=0);
    token=open_session();assert(!chunk(token,0,11));now+=5000001;assert(chunk(token,11,11)!=0);
    token=open_session();stage(token);bms_afe_hw_access_close();assert(commit(token)!=0);
    token=open_session();stage(token);u16 newer=open_session();assert(newer!=token);assert(commit(token)!=0);
    token=open_session();frame[78]^=1;stage(token);assert(commit(token)!=0);frame[78]^=1;assert(writes==1);
    token=open_session();frame[2]=0x21;inner_crc();stage(token);assert(commit(token)!=0);assert(writes==1);
    frame[2]=0x25;inner_crc();token=open_session();stage(token);apply_error=4;assert(commit(token)==4);assert(writes==2);
    assert(commit(token)!=0 && writes==2);apply_error=0;
    now=UINT32_MAX-100;token=open_session();stage(token);now+=5000001;assert(commit(token)!=0 && writes==2);
    token=open_session();stage(token);now+=60000001;assert(commit(token)!=0);
    puts("PASS AFE fragments: <=20-byte packets, single commit, partial/duplicate/order/token/CRC/address rejection, timeout/wrap/disconnect, failed commit no replay");
    return 0;
}
