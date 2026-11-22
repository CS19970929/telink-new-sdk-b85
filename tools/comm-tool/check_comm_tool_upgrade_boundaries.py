"""校验实际缓存准入策略与PC镜像边界，不访问Flash或串口。"""
import ctypes
import os
from pathlib import Path
import re
import struct
import tempfile
from check_comm_tool_owc import APP, build_library
from comm_tool_host import load_image, BMS_APP_BASE_ADDR, BMS_APP_FLASH_LIMIT, COMM_TOOL_APP_BASE_ADDR, COMM_TOOL_APP_FLASH_LIMIT

def function(source,name):
    match=re.search(r"(?:static )?(?:int|void|uint32_t|const CtFirmwareInfo \*)\s*"+name+r"\s*\([^)]*\)\s*\{",source)
    if not match:raise ValueError(name)
    start=match.start();at=match.end();depth=1
    while depth:
        if source[at]=="{":depth+=1
        elif source[at]=="}":depth-=1
        at+=1
    return source[start:at]

def main():
    parent=Path(os.environ["LOCALAPPDATA"])/"CodexTemp/comm-tool"
    parent.mkdir(parents=True,exist_ok=True)
    temp=Path(tempfile.mkdtemp(prefix="upgrade-boundary-",dir=parent))
    source=(APP/"ct_flash_store.c").read_text(encoding="utf-8")
    defines="\n".join(re.findall(r"^#define CT_FW_(?:MAGIC|VALID)\s+.*$",source,re.M))
    init=function(source,"CtFlash_Init").replace("(const CtFirmwareInfo *)CT_FW_META_PAGE","&descriptor")
    capacity=function(source,"CtFlash_C073Capacity").replace("(const uint32_t *)CT_FW_CACHE_BASE","cache")
    model=(
        '#include <stdint.h>\n#include <string.h>\n#include "ct_config.h"\n#include "ct_flash_store.h"\n'
        '#include "ct_upgrade_manager.h"\n#include "ct_can_gateway.h"\n#include "ct_debug_log.h"\n'+defines+
        "\nstatic CtFirmwareInfo s_fw_info,descriptor;\n"+
        function(source,"app_addr_supported")+"\n"+init+"\n"+function(source,"CtFlash_GetInfo")+"\nstatic uint32_t cache[70];\n"+
        function(source,"c073_image_capacity")+"\n"+capacity+r"""
int ModelCached(uint32_t address,uint32_t size) {
 memset(&descriptor,0,sizeof(descriptor));
 descriptor.magic=CT_FW_MAGIC;descriptor.valid=CT_FW_VALID;
 descriptor.app_addr=address;descriptor.size=size;
 CtFlash_Init();
 return CtFlash_GetInfo()->valid==CT_FW_VALID;
}
static uint32_t now;
static uint16_t advertised;
static uint8_t already_boot, entered, starts;
uint32_t CtBoard_GetTickMs(void) { return now; }
#if CT_DEBUG_LOG_ENABLE
void CtDebugLog_Record(uint8_t m,uint8_t e,uint16_t v,uint16_t w) { (void)m;(void)e;(void)v;(void)w; }
#endif
int CtCan_AppReadRegs(uint8_t node,uint16_t reg,uint16_t count,uint16_t *words) {
 const uint16_t identity[12]={0x5550,1,0xC073,13,2,2,0x0800,0x3000,0,advertised,0,0};
 if (node || reg!=0xD170u || count!=12u || !advertised) return 0;
 memcpy(words,identity,sizeof(identity));
 return 1;
}
int CtCan_AppEnterIap(uint8_t node) { if (node) return 0; ++entered; return 1; }
int CtCan_IapIsC073(void) { return 1; }
int CtCan_IapSendHello(uint8_t node) { return node==1u; }
int CtCan_IapSendStart(uint8_t node,uint32_t size,uint16_t crc) {
 (void)size;(void)crc;
 if (node!=1u) return 0;
 ++starts;
 return 1;
}
uint8_t CtCan_IapPollAck(uint8_t node,uint8_t cmd,uint16_t *sequence,uint8_t *code) {
 (void)node;
 *sequence=0;*code=0;
 return cmd==CT_CAN_IAP_HELLO && (already_boot || entered) ? CT_CAN_IAP_ACK_MATCH_OK : 0;
}
int CtCan_IapSendData(uint8_t n,uint16_t s,const uint8_t *data) { (void)n;(void)s;(void)data;return 0; }
int CtCan_IapSendCommit(uint8_t n,uint16_t s,uint16_t l,uint16_t c) { (void)n;(void)s;(void)l;(void)c;return 0; }
int CtCan_IapSendEnd(uint8_t n,uint16_t count,uint16_t crc) { (void)n;(void)count;(void)crc;return 0; }
int CtFlash_Read(uint32_t offset,uint8_t *data,uint16_t length) { (void)offset;(void)data;(void)length;return 0; }
int ModelC073(uint32_t version, uint32_t bytes, uint16_t app_capacity, int boot) {
 unsigned step;
 if (!ModelCached(CT_C073_APP_BASE,bytes)) return -1;
 cache[0x104u/4u]=CT_C073_BOARD;
 cache[0x108u/4u]=CT_C073_APP_BASE;
 cache[0x10Cu/4u]=version;
 advertised=app_capacity;
 already_boot=(uint8_t)boot;
 now=entered=starts=0;
 CtUpgrade_Init();
 if (!CtUpgrade_Start(1u)) return CtUpgrade_GetStatus()->last_error;
 for (step=0;step<16u;++step) {
     CtUpgrade_Task();
     now+=800u;
     if (starts) return entered ? 0x101 : 0x100;
     if (CtUpgrade_GetStatus()->last_error) break;
 }
 return (entered ? 0x200 : 0) | CtUpgrade_GetStatus()->last_error;
}
""")
    (temp/"flash_model.c").write_text(model,encoding="utf-8")
    lib=ctypes.CDLL(str(build_library(temp,[temp/"flash_model.c",APP/"ct_upgrade_manager.c"],("ModelCached","ModelC073"))))
    lib.ModelCached.argtypes=[ctypes.c_uint32,ctypes.c_uint32]
    lib.ModelCached.restype=ctypes.c_int
    checks=0
    for base,limit in ((BMS_APP_BASE_ADDR,BMS_APP_FLASH_LIMIT),(COMM_TOOL_APP_BASE_ADDR,COMM_TOOL_APP_FLASH_LIMIT)):
        size=limit-base
        assert lib.ModelCached(base,size)==1
        assert lib.ModelCached(base,size+1)==0
        assert lib.ModelCached(base,0)==0
        assert lib.ModelCached(base,0xffffffff)==0
        checks+=4
        data=struct.pack("<II",0x20001000,base+1)+bytes(size-8)
        file=temp/"test.image";file.write_bytes(data)
        assert len(load_image(file,base))==size;checks+=1
        file.write_bytes(data+b"\0")
        try:load_image(file,base)
        except SystemExit:checks+=1
        else:raise AssertionError("越界镜像被接受")
    for base in (0x08000000,0x08004801,0x08018000):
        assert lib.ModelCached(base,128)==0;checks+=1
    lib.ModelC073.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint16,ctypes.c_int]
    lib.ModelC073.restype=ctypes.c_int
    for version,size,capacity,boot,result in (
        (0x20000,44*1024,0xB000,0,0x101), (0x10000,46*1024,0xB800,0,0x101),
        (0x20000,44*1024,0xB800,0,0x23), (0x10000,44*1024,0xB000,0,0x23),
        (0x20000,45*1024,0xB000,0,0x23), (0x30000,1024,0xB000,0,0x23),
        (0x20000,1024,0,1,0x23), (0x10000,1024,0,1,0x100),
    ):
        assert lib.ModelC073(version,size,capacity,boot)==result
        checks+=1
    assert BMS_APP_FLASH_LIMIT==0x0801f800
    print("UPGRADE_BOUNDARY_PASS checks="+str(checks),temp)
if __name__=="__main__":main()
