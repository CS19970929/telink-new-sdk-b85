#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/common/ble_flash.h"
#include "store.h"
#include "link.h"

#if D008_FLASH_TEST
#include "tests/flash_board.h"
#define NOR_OFFSET 0x20000u
#else
#define NOR_OFFSET 0u
#endif

static bool allowed(uint32_t addr, unsigned n)
{
    const uint32_t bases[] = {0x40000,0x44000,0x4c000};
    for (unsigned i=0;i<3;++i)
        if (n && n<=8192 && addr>=bases[i] && addr-bases[i]<=8192u-n) return true;
    return false;
}

static bool witness(void)
{
    uint32_t mark=0;
    if (ota_program_offset!=0 && ota_program_offset!=0x20000) return false;
    uint32_t active=ota_program_offset ? 0 : 0x20000;
    flash_read_page(active+8,4,(u8 *)&mark);
    return mark==0x544c4e4b;
}

bool nor_read(uint32_t addr,void *out,unsigned n)
{
    if (!allowed(addr,n) || blc_flash_capacity!=FLASH_SIZE_512K || !witness()) return false;
    flash_read_page(addr+NOR_OFFSET,n,out);
    return true;
}

static bool modify(uint32_t addr,const void *data,unsigned n)
{
    if (!allowed(addr,n) || update_active() || wired_transmitting() || !witness() ||
        blc_flash_capacity!=FLASH_SIZE_512K || flash_read_mid()!=blc_flash_mid) return false;
    switch (blc_flash_mid) {
    case 0x1360c8: case 0x13325e: case 0x134051: case 0x136085: case 0x1360eb: break;
    default: return false;
    }
    bool connected=blc_ll_getCurrentState()==BLS_LINK_STATE_CONN;
    if (connected) {
        /* 官方 API 以 ms 授予 BRX 空窗；擦除延后到断开连接。 */
        if (!data || bls_ll_requestConnBrxEventDisable()<10) return false;
        bls_ll_disableConnBrxEvent();
    }
#if D008_FLASH_TEST
    if (data) ++flash_test_programs;
    else ++flash_test_erases;
#endif
    addr+=NOR_OFFSET;
    if (data) flash_write_page(addr,n,(u8 *)data);
    else flash_erase_sector(addr);
    if (connected) bls_ll_restoreConnBrxEvent();
    return !(flash_read_status(FLASH_READ_STATUS_CMD_LOWBYTE)&1u) && witness();
}

bool nor_write(uint32_t addr,const void *data,unsigned n)
{
    return n && n<=256u-(addr&255u) && modify(addr,data,n);
}
bool nor_erase(uint32_t addr)
{
    return !(addr&4095u) && modify(addr,0,4096);
}
