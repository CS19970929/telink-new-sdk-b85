# 冗余接口、请求读取与 SOC 提交简化

基线：`549dc804b7d7fcc7fd7bfd81d61aa5b676d1cc06`；分支：`codex-bms-deep-simplification-20261016`。用户授权实施前三项并分别中文提交，第四项 OTA 参数更新执行逻辑暂缓。四产品源清单、产品参数、OTA 保参选择及标识不变；只执行相关 host 与 sources/link/resources，不生成 BIN。

## 1. 删除冗余接口和版本 RAM 缓存

删除 `bms_config_store_set_soc()`：上一轮已取消其唯一生产调用，剩余两个调用仅用于存储夹具。将夹具的身份一致性和保存失败不发布检查转到现存 `bms_config_store_set_system()`；保留 SOC getter、完整字段校验、CFG2 编解码及曲线编辑入口。没有删除持久字段，也不改变旧记录读取。

硬件/软件版本由原编译字符串直接编码到原 16-word 窗口，删除两个 32-byte RAM 数组和启动复制；版本内容、截断范围、大端编码、NUL/尾部补零均保持。可写 SN 仍有独立 32-byte 缓存，启动及 SN 提交后按原入口刷新，Config 读取失败仍回到编译默认。

新增完整 48-word 身份窗口、独立版本窗口、非法尾地址及空/完整 32-byte/短 SN/读取失败场景。删除函数可能原已被链接器回收；64 B 是删掉的缓存对象大小，实际 Flash/RAM 变化以本轮最终链接比较为准。

阶段相关 host 为四产品 core contract、Modbus、语义存储共 12 组，0 失败。另用同一组协议期望分别编译基线与本阶段完整 Modbus 模块：D008 2042 帧、SH 各 2043 帧，逐帧字节/CRC 检查及汇总结果完全一致；只省略读取次数断言，不省略协议断言。

## 2. AFE 配置按请求读取一次

`0x2500..0x252B` 的 requested 数据和 VALID 元数据共用本次请求的一份配置；仅含其他元数据时不获取配置。35-word 数据读取由 35 次 70-byte 复制/校验减少为一次；44-word 完整窗口原有 36 次 getter 也只调用一次。SH `0x2180..0x218A` actual 窗口从逐字获取改为一次获取已验证缓存，仍逐字段显式编码，不增加 SPI。effective 既有整帧回读保持。

这两个窗口前后都有非法地址间隔，整帧入口先验证窗口末端再取值；通用窗口继续完整预检后按原顺序读取。未知数据仍为 `0xFFFF`，VALID 仍为 0/1，下一请求重新获取，不增加全局缓存。所有 getter 和协议处理同步在主循环执行；没有修改 AFE 寄存器、保护恢复或输出资格。

阶段四产品 Modbus/参数块 host 共 8 组通过。新增 requested 44-word 窗口的全部 990 个子区间 × 有效/无效两种状态、元数据免读取、非法前后边界、跨请求更新，以及 SH actual 全部 66 个子区间 × 两种状态和下一帧更新。有效配置原有 630 个子区间及故障场景继续执行，parser 100000 帧畸形输入保留。使用同一稳定 RAM 所有者夹具编译基线和当前完整 Modbus：D008 3361 帧、SH 各 3495 帧的逐帧期望与汇总一致；只忽略 getter 次数断言，正常 host 另行断言一次读取。

## 3. SOC 提交步骤和类型切换备份

set/reset 共用私有 `save_profile()`，集中编码、journal 保存、成功后发布曲线、持久标志和 generation；保存失败的错误上报也只有一处。set 仍校验 Config 身份并对相同已保存曲线去重；reset 仍允许先于 Config 改类型，同样默认也强制写入并更新 generation。set 的稳定候选副本继续保留，不把 Flash 事务中的对象改成可变外部指针。

化学体系切换从两个 100-byte 曲线副本和两次 getter，减少为一个原曲线副本和一次 getter；同一副本用于新鲜静置电流门禁及 Config 失败回滚。关闭输出不会修改 SOC 曲线，reset 才会发布新曲线；因此必须保留原值的复制，不能用指向已提交对象的指针替代。仍先关闭输出、保存曲线、提交 Config、加载运行值并应用 AFE；任何失败仍走原返回码和 inhibit。Config/SocProfile 分域提交仍非跨域原子事务。

新增 reset 的 LFP/NMC 各 132 个写入字节断点、失败不发布/重启保持旧曲线、身份不同时允许 reset、generation 回绕跳零、相同默认强制保存、读故障后禁止 set/reset。现有 set 的相同值去重和 132 个断点继续执行；用基线与当前完整 profile/journal 编译同一夹具，所有断言和实际 Flash 编程字节流汇总一致。语义存储还检查自定义原曲线在 reset/Config 失败后的恢复，及 ±123 mA 边界/124 mA 拒绝、单次 getter 和输出 inhibit。

阶段四产品 profile/journal、语义存储及 D014 真实安全闭环共 9 组通过。profile 基线/终态四产品比较均为 `flash-stream=95b556d5`、508 次编程调用；这属于 RAM Flash 故障模型证据。

## 验证边界

本轮资源基线重新链接，D008 使用源码 CUSTOM 20S LFP，D011/D013 为 10S LFP，D014 为 8S LFP；同时记录 development 与 production。基线相关 host 为 29 组、0 失败，属于限定范围验证。后续阶段及最终固定提交收据在完成后追加。

host RAM Flash、协议替身和 ELF/MAP 不替代实板 Flash 掉电、物理保护恢复、MOS Gate/Vgs、UART/BLE 电气与吞吐、WDT/低功耗/唤醒、OTA 和实际客户端联调；不据此宣称硬件验收完成。
