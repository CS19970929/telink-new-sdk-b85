"""真实C解码器和board_owc.c回放；不访问串口、不生成镜像。"""
import ctypes
import os
from pathlib import Path
import random
import re
import struct
import tempfile
from check_comm_tool_owc import APP, build_library

def main():
    parent=Path(os.environ["LOCALAPPDATA"])/"CodexTemp/comm-tool"
    parent.mkdir(parents=True,exist_ok=True)
    temp=Path(tempfile.mkdtemp(prefix="dual-wire-",dir=parent))
    source=(APP.parent/"bsp/board_owc.c").read_text(encoding="utf-8")
    source=re.sub(r'^#include[^\n]*','',source,flags=re.M)
    model=r"""
#include <stdint.h>
#include "ct_owc.h"
#define CT_COMM_UART_PORT 3
#define CT_COMM_UART_PORT_USART3 3
#define EXTI_Line11 2048u
#define EXTI15_10_IRQn 40u
#define GPIO_Pin_11 2048u
#define __DMB() ((void)0)
#define NVIC_DisableIRQ(x) ((void)(x))
#define NVIC_EnableIRQ(x) ((void)(x))
static struct {uint32_t PR;} exti;
static struct {uint32_t IDR;} gpio;
static struct {uint32_t CNT;} timer;
#define EXTI (&exti)
#define GPIOC (&gpio)
#define TIM2 (&timer)
static uint32_t model_ms;
static uint32_t Board_GetTickMs(void) {return model_ms;}
""" + source + r"""
void ModelReset(uint32_t now_ms,uint16_t timer_us,uint8_t mode) {
 model_ms=now_ms; timer.CNT=timer_us;
 s_head=s_tail=0; s_lost=0; s_enabled=1;
 s_last_ms=s_lease_ms=now_ms; s_last_us=timer_us;
 (void)CtOwc_SetMode(mode);
}
void ModelEdge(uint32_t now_ms,uint16_t timer_us,uint8_t level) {
 model_ms=now_ms; timer.CNT=timer_us;
 gpio.IDR=level ? GPIO_Pin_11 : 0;
 exti.PR=EXTI_Line11;
 EXTI15_10_IRQHandler(); BoardOwc_KeepAlive(); BoardOwc_Poll();
}
"""
    (temp/"board_model.c").write_text(model,encoding="utf-8")
    lib=ctypes.CDLL(str(build_library(temp,[temp/"board_model.c"])))
    lib.ModelReset.argtypes=[ctypes.c_uint32,ctypes.c_uint16,ctypes.c_uint8]
    lib.ModelEdge.argtypes=[ctypes.c_uint32,ctypes.c_uint16,ctypes.c_uint8]
    lib.CtOwc_Poll.argtypes=[ctypes.c_uint32]
    lib.CtOwc_SetMode.argtypes=[ctypes.c_uint8]
    lib.CtOwc_SetMode.restype=ctypes.c_uint8
    lib.CtOwc_GetMode.restype=ctypes.c_uint8
    for name in ("CtOwc_EncodeSnapshot","CtOwc_EncodeSnapshotV3"):
        method=getattr(lib,name)
        method.argtypes=[ctypes.c_uint8,ctypes.c_uint8,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint16]
        method.restype=ctypes.c_uint16
    clock=0; checks=0; last_level=None
    def reset(mode,base=0):
        nonlocal clock,last_level
        clock=base;last_level=None
        lib.ModelReset((clock//1000)&0xffffffff,clock&65535,mode)
    def check(value,name):
        nonlocal checks
        assert value,name
        checks+=1
        print("PASS",name)
    def edge(duration,level):
        nonlocal clock,last_level
        clock+=duration
        # EXTI只产生真实电平变化；帧间低电平与同步低电平可能合并。
        if level!=last_level:
            lib.ModelEdge((clock//1000)&0xffffffff,clock&65535,level)
            last_level=level
    def poll(delay):
        nonlocal clock
        clock+=delay*1000
        lib.CtOwc_Poll((clock//1000)&0xffffffff)
    def snapshot(selector=0,legacy=False,capacity=512):
        out=ctypes.create_string_buffer(512)
        fn=lib.CtOwc_EncodeSnapshot if legacy else lib.CtOwc_EncodeSnapshotV3
        n=fn(selector,1,(clock//1000)&0xffffffff,out,capacity)
        return out.raw[:n]
    def frame(raw,protocol,jitter=0):
        nonlocal clock
        edge(100000,0)
        edge(62000 if protocol==2 else 10000,1)
        edge(2000 if protocol==2 else 1000,0)
        for byte in raw:
            for bit in (range(7,-1,-1) if protocol==2 else range(8)):
                one=bool(byte&(1<<bit))
                low=(2000 if one else 4000) if protocol==2 else (500 if one else 1500)
                period=6000 if protocol==2 else 2000
                delta=random.randint(-jitter,jitter)
                edge(low+delta,1);edge(period-low-delta,0)
        edge(25000 if protocol==2 else 5000,1)
        if protocol==2:
            edge(2000,0) # 必须在真实2ms下降沿完成发布，不能等待50ms。
        else:
            poll(51)
    def lishen(soc=80,voltage=76,temp=-20,bad=False,reserved=0):
        p=bytes([0,soc,voltage,temp&255,reserved])
        return p+bytes([((sum(p)&255)^(1 if bad else 0))])
    def a133():
        p=bytes([0x3a,0x13,29,160,0x08,0x02,0xec,0x13])+bytes(24)
        return p+bytes([sum(p)&255])
    random.seed(20261116)
    reset(2)
    raw=lishen()
    frame(raw,2)
    s=snapshot(0x80)
    check(len(s)==42 and s[36:]==raw,"board实际计时保留62ms同步并按MSB解码6字节")
    check(s[0]==3 and s[32:36]==bytes([2,2,2,0]),"新快照显式区分模式、锁定协议和报文协议")
    check(s[30:32]!=b"\0\0","固定模式首个有效报文取得会话")
    check(not snapshot(0,legacy=True),"新协议不会流入旧v2快照")
    check(not snapshot(0x7f) and not snapshot(0,capacity=35),"非法selector和短buffer被拒绝")
    reset(0)
    frame(raw,2)
    check(snapshot()[33]==0 and len(snapshot())==36,"自动模式单帧不授权实时数据")
    frame(lishen(bad=True),2)
    frame(raw,2)
    check(snapshot()[33]==0,"坏SUM打断自动确认")
    frame(raw,2,jitter=120)
    check(snapshot()[33]==2 and snapshot(0x80)[36:]==raw,"两帧确认且接收合法抖动")
    old_session=struct.unpack_from("<H",snapshot(),30)[0]
    poll(5100)
    check(snapshot()[33]==0 and len(snapshot(0x80))==36,"失联解除自动锁定并清空旧协议缓存")
    frame(a133(),1);frame(a133(),1)
    s=snapshot(0x3a)
    check(s[33]==1 and s[36:]==a133(),"失联后可自动重新识别A1.3.3")
    check(struct.unpack_from("<H",s,30)[0]!=old_session,"跨协议恢复切换会话")
    check(len(snapshot(0x80))==36,"切到A1.3.3后不残留锂神测量")
    reset(1)
    frame(raw,2)
    check(snapshot()[35]==2 and len(snapshot())==36,"固定A1.3.3诊断协议不匹配但不展示数据")
    frame(a133(),1)
    check(snapshot()[35]==0 and snapshot(0x3a,legacy=True)[0]==2,"旧控制模式及v2合同仍可用")
    reset(2)
    frame(lishen(soc=101),2)
    check(len(snapshot(0x80))==36,"非法SOC不发布")
    frame(lishen(reserved=1),2)
    check(len(snapshot(0x80))==36,"非法保留字段不发布")
    frame(lishen(bad=True),2)
    check(len(snapshot(0x80))==36 and struct.unpack_from("<I",snapshot(),12)[0]>0,"坏校验不进入有效缓存")
    frame(raw[:-1],2)
    check(len(snapshot(0x80))==36,"截断帧不发布")
    frame(raw+b"\0",2)
    check(len(snapshot(0x80))==36,"多余字节不发布")
    for temp_c in (-128,-1,0,127):
        reset(2);value=lishen(temp=temp_c);frame(value,2)
        check(snapshot(0x80)[36:]==value,"有符号温度原始字节完整:"+str(temp_c))
    reset(0,base=(2**32-200)*1000)
    frame(raw,2);frame(raw,2)
    check(snapshot()[33]==2 and struct.unpack_from("<I",snapshot(),4)[0]<10,"毫秒tick回绕和16位计时器回绕")
    check(lib.CtOwc_SetMode(9)==0 and lib.CtOwc_GetMode()==0,"非法模式不改变接收状态")
    print("DUAL_OWC_PASS checks="+str(checks),temp)
if __name__=="__main__":
    main()
