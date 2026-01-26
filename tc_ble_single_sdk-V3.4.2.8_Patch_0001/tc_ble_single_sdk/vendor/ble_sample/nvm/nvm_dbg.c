#include "nvm_dbg.h"

void nvm_dbg_hexdump(const char *tag, const void *data, unsigned int len)
{
    if(NVM_LOG_LEVEL < 5 || !data || !len){
        return;
    }
    const uint8_t *p = (const uint8_t *)data;
    NVM_PRINTF("[NVM][T] %s len=%u\r\n", tag ? tag : "dump", (unsigned)len);
    for(unsigned int i=0;i<len;i++){
        if((i % 16) == 0){
            NVM_PRINTF("[NVM][T] %04u: ", (unsigned)i);
        }
        NVM_PRINTF("%02X ", p[i]);
        if((i % 16) == 15 || i == len-1){
            NVM_PRINTF("\r\n");
        }
    }
}
