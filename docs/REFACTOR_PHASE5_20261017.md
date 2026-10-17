# 冗余接口、请求读取与 SOC 提交简化

基线：`549dc804b7d7fcc7fd7bfd81d61aa5b676d1cc06`；分支：`codex-bms-deep-simplification-20261016`。用户授权实施前三项并分别中文提交，第四项 OTA 参数更新执行逻辑暂缓。四产品源清单、产品参数、OTA 保参选择及标识不变；只执行相关 host 与 sources/link/resources，不生成 BIN。

## 1. 删除冗余接口和版本 RAM 缓存

删除 `bms_config_store_set_soc()`：上一轮已取消其唯一生产调用，剩余两个调用仅用于存储夹具。将夹具的身份一致性和保存失败不发布检查转到现存 `bms_config_store_set_system()`；保留 SOC getter、完整字段校验、CFG2 编解码及曲线编辑入口。没有删除持久字段，也不改变旧记录读取。

硬件/软件版本由原编译字符串直接编码到原 16-word 窗口，删除两个 32-byte RAM 数组和启动复制；版本内容、截断范围、大端编码、NUL/尾部补零均保持。可写 SN 仍有独立 32-byte 缓存，启动及 SN 提交后按原入口刷新，Config 读取失败仍回到编译默认。

新增完整 48-word 身份窗口、独立版本窗口、非法尾地址及空/完整 32-byte/短 SN/读取失败场景。删除函数可能原已被链接器回收；64 B 是删掉的缓存对象大小，实际 Flash/RAM 变化以本轮最终链接比较为准。

阶段相关 host 为四产品 core contract、Modbus、语义存储共 12 组，0 失败。另用同一组协议期望分别编译基线与本阶段完整 Modbus 模块：D008 2042 帧、SH 各 2043 帧，逐帧字节/CRC 检查及汇总结果完全一致；只省略读取次数断言，不省略协议断言。

## 验证边界

本轮资源基线重新链接，D008 使用源码 CUSTOM 20S LFP，D011/D013 为 10S LFP，D014 为 8S LFP；同时记录 development 与 production。基线相关 host 为 29 组、0 失败，属于限定范围验证。后续阶段及最终固定提交收据在完成后追加。

host RAM Flash、协议替身和 ELF/MAP 不替代实板 Flash 掉电、物理保护恢复、MOS Gate/Vgs、UART/BLE 电气与吞吐、WDT/低功耗/唤醒、OTA 和实际客户端联调；不据此宣称硬件验收完成。
