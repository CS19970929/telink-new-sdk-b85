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
    match=re.search(r"(?:static )?(?:int|void|const CtFirmwareInfo \*)\s*"+name+r"\s*\([^)]*\)\s*\{",source)
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
    model=(
        '#include <stdint.h>\n#include <string.h>\n#include "ct_config.h"\n#include "ct_flash_store.h"\n'+defines+
        "\nstatic CtFirmwareInfo s_fw_info,descriptor;\n"+
        function(source,"app_addr_supported")+"\n"+init+"\n"+function(source,"CtFlash_GetInfo")+r"""
int ModelCached(uint32_t address,uint32_t size) {
 memset(&descriptor,0,sizeof(descriptor));
 descriptor.magic=CT_FW_MAGIC;descriptor.valid=CT_FW_VALID;
 descriptor.app_addr=address;descriptor.size=size;
 CtFlash_Init();
 return CtFlash_GetInfo()->valid==CT_FW_VALID;
}
""")
    (temp/"flash_model.c").write_text(model,encoding="utf-8")
    lib=ctypes.CDLL(str(build_library(temp,[temp/"flash_model.c"],("ModelCached",))))
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
    assert BMS_APP_FLASH_LIMIT==0x0801f800
    print("UPGRADE_BOUNDARY_PASS checks="+str(checks),temp)
if __name__=="__main__":main()
