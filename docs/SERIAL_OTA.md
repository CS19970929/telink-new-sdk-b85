# TLSR8251 四产品 UART／RS485 升级

2026-10-21实现。四产品共同使用`bms/core/bms_serial_ota.c`和
`bms/platform/telink/bms_serial_ota_telink.c`；四份`sources.txt`显式列入，保留原链接顺序。

## 使用入口与兼容性

Windows内部版复用原有OTA升级页、选择BIN、开始/取消及升级后通信检查。
当前连接为BLE时沿用SDK OTA，为串口时选择“Telink BLE / UART / RS485”。
Auto按镜像架构选择，串口在擦除前还会只读探测能力并核对产品。STM32 IAP路径保留。
客户版原先不开放OTA，保持该版本边界；没有另建客户端。
Windows仍在既有`bms-tool-windows/`维护，本轮实际工作分支是
`codex-soc-host-config-20261020`，不清理、不提交已有无关修改。

BLE与UART使用**同一份标准构建、tl_check_fw2处理的正式BIN**，没有串口专用打包步骤。
新增20字节只读镜像描述，不改变启动头、ABI、Flash分区、参数协议v2、
Config/Event schema4、State schema5、OTA更新组及一次性ID。
参数仍按BIN内选择处理，见[OTA参数](OTA_PARAMETERS.md)、[存储](STORAGE.md)及
[SOC迁移](SOC_REFACTOR_20261020.md)。传输方式本身不触发恢复出厂。
旧APP须先通过一次BLE或既有烧录安装支持版。串口要求镜像包含新版描述；
旧BIN仍可走原BLE入口，其存储回退兼容仍按上述文档处理。

## 硬件、双槽与BLE边界

目标为TLSR8251/32 KiB SRAM/`MCU_STARTUP_8251`。D008沿用UART/一线通复用，D013为UART，
D011/D014为RS485；D014保留通信供电、唤醒首包隔离和DE控制。按连接时波特率发送，不在升级中改速。

保持槽A `0x00000..0x1EFFF`、槽B `0x20000..0x3EFFF`，每槽124 KiB。
末尾4 KiB保留区及业务、MAC、配对和射频校准区不写入。
SDK公开的`ota_program_offset`是候选槽，为0时当前运行高槽，否则运行低槽；配置高地址须为`0x20000`。
`blc_ota_getCurrentUsedMultipleBootAddress()`只返回配置，不能据此判断活动槽。

页面、镜像、Flash底层和安全互锁共用。SDK `otaWrite`接收GATT请求，其完成流程依赖BLE通知和断开，
不能直接拿其返回值确认UART写入。BLE薄入口`app_ble_ota_write`只在串口忙时阻止交给SDK；
其余BLE协议、通知和升级路径不变。串口BEGIN检查`ota_is_working`、布局和Flash会话，双通道互斥。

官方依据：[Uart_fw_update](https://doc.telink-semi.cn/doc/en/software/res/sdk/2.4GHz%20Proprietary/telink_2.4ghz_proprietary_sdk_en/telink_2.4ghz_proprietary_sdk_developer_handbook_en/#uart_fw_update)、
[TC BLE OTA](https://doc.telink-semi.cn/doc/zh/software/res/sdk/ble/tc_ble_cn/tc_ble_single_connection_cn/#ota)。
本地`drivers/B85/lib/include/pm.h:232–234`公开槽变量，`vendor/common/flash_fw_check.c:40–64`说明其含义。
[AN-21112301-C2](https://wiki.telink-semi.cn/doc/an/AN-21112301-C_Telink%20B85m%20BLE%20Single%20Connection%20SDK%20Developer%20Handbook.pdf)
7.1.1 p220、7.1.2 pp221–222、7.3.4 pp240–241介绍启动标志与掉电原则；旧手册API及默认布局不能覆盖当前SDK事实。

## 串口协议v1

新增私有功能码`0x46`只在UART分派，不占用既有Modbus寄存器；仅从站1响应，广播不擦写。
新增多字节字段little-endian，帧使用原Modbus CRC16低字节在前；既有寄存器字序不变。

请求：`[slave=1, function=0x46, command:u8, sequence:u16, session:u32, payload..., crc16]`。
长度11..143字节。sequence逐命令回显；session由上位机随机生成且非零。

| command | payload | 含义 |
|---|---|---|
| 0 QUERY | 无 | session0只读探测，非零检查会话；无擦写 |
| 1 BEGIN | product:u8、size:u32、build:u32、descriptor_offset:u32、image_trailer:u32 | 17字节；验证后占有Flash，分步擦31个固件扇区 |
| 2 DATA | offset:u32、1..128字节镜像数据 | 首包至少32字节；偏移连续，相同旧包只读比较后确认 |
| 3 FINISH | 无 | 收齐后分步读回、验证完整CRC和描述，不提交 |
| 4 COMMIT | 无 | 仅READY可用；激活并回读新区标志，再使旧区失效并回读 |
| 5 ABORT | 无 | 撤销未提交的本会话并恢复锁；COMMITTED不可撤销 |

固定30字节应答：

| 偏移 | 字段 |
|---|---|
| 0..4 | 回显slave、function、command、sequence |
| 5/6/7 | state/error/product，u8 |
| 8/12/16 | session/next_offset/image_size，u32 |
| 20/24 | 当前运行Build ID/镜像尾部CRC，u32 |
| 28..29 | CRC16 |

state依次为0 IDLE、1 ERASING、2 RECEIVING、3 VERIFYING、4 READY、5 COMMITTED、6 ERROR。
error依次为0正常、1非法帧/命令、2忙或布局不支持、3会话不符、4状态/偏移非法、5镜像错误、6Flash错误、7超时。
QUERY可读ERROR，新合法BEGIN可重新开始；其它会话不能写入或取消当前会话。

描述连续20字节：ASCII `BMSU8251`（8）、版本1（u8）、内部product（u8）、布局1（u16）、
Build ID（u32）、槽上限126976（u32）。上位机要求唯一合法描述；设备在最终校验时再次验证。
描述用于产品/布局/身份核对，不是数字签名。
BIN的`0x18`长度及4字节Telink CRC32保持原样。以FFFFFFFF开始，反射多项式EDB88320，
完整镜像未异或CRC应为0；传输时仅`+8`保持FF，其余TLNK字节正常写入并校验。
最终只提交单字节`+8=0x4B`，避免多字节标志写一半时硬件提前接受候选槽。

## 调度、故障和验收

采样/保护在升级服务之前；ISR只交付DMA和状态。每轮最多擦一个4 KiB扇区或校验128字节，
页编程按256字节页边界拆分并读回。15秒无有效会话活动或10分钟总时长即退出，使用无符号毫秒差处理回绕。
业务存储继续被现有OTA/Flash锁拒绝；普通休眠沿用OTA门禁，强制保护性休眠仍可中断。
D008会话期间保持UART所有权，不因1秒静默切回一线通；D014有效升级会话期间保持RS485通信域供电，
会话结束或超时后恢复正常静默断电。该门禁不改变保护性强制休眠入口。

仅无副作用QUERY可为唤醒重试。写命令等待CRC、命令、sequence及session匹配的应答；
丢失确认后停止，不重发DATA或COMMIT。ABORT只是取消未提交会话。
提交确认后约1秒且UART不忙时重启；上位机先确认新程序IDLE、产品、Build和镜像CRC，
再用原页面读取身份和实时数据。提交期间超时/断线/取消不能证明未切换，报告结果未知。
新程序SDK仍会清理旧固件区，双槽提交机制不等于运行异常自动回滚，保留既有有线救援路径。

验证入口：`tests/serial_ota_host_check.py`运行真实core及Telink端口，RAM模型覆盖最大镜像、
A/B、重复/乱序、CRC、写入故障、提交逐字节中断、回绕和BLE占用拒绝。
相关四产品storage/Modbus/Flash/UART/PM、D008复用、开发/生产link/resources按本轮收据记录。
Windows新增`test-telink-serial-ota.ps1`并保留原`test-ota-protocol.ps1`；客户/内部/CLI均检查net8.0与net48。
初始实现固定提交`9a38c13a`仅完成host和ELF/MAP验证，未生成固件BIN或操作设备。

初始实现提交时未执行实板UART/RS485吞吐及唤醒、BLE互斥、AFE采样/保护最坏时延、SDK Flash忙等待和watchdog、
真实Flash/低电压掉电、双槽物理升级、Gate输出及低功耗恢复验收。软件模型、编译和MAP不代替这些结论。

## 2026-10-21 D014 开发板串口实测

用户随后授权对`BT_cs001`（`A4:C1:38:6C:DD:23`）、`COM3 / 115200 8N1`进行开发版实测，
并明确要求暂停全部休眠以避免 AFE 通信故障触发深睡。生产板和生产镜像不在本次操作范围。

D014 在固定提交`b5f6c48e`中曾以`BMS_PRODUCT_SERIAL_OTA_TEST_NO_SLEEP`暂停普通 Suspend、
保护性/命令/开关深睡及启动深睡保持，并令 RS485 通信域保持供电。采样、AFE 故障确认与输出禁止保留。
当时生产构建禁止开启，其余三个产品默认关闭。该测试状态持续耗电，不能用于低功耗验收。
随后按用户要求从源码完整移除该开关及PM旁路，恢复D014开发/生产的正常休眠和RS485供电策略。
源码恢复不改变板上既有程序；本次整理代码和上位机发布未重新生成或下发固件，板上仍为禁休眠开发测试版。

先经 BLE 从`2b35e1ee`升级到含串口 OTA 的开发测试镜像`9a38c13a / dirty`，收到成功确认并用 COM3
读取到新 Build。随后通过串口传输**同一份116164字节 BIN**，镜像 SHA-256 为
`d7905493ee82727a92955880c5172b6effcde421afe067d8fa67951e08307ba6`，尾部CRC为`e2b2ffc9`。
串口完整读回/CRC、启动标志提交、新程序IDLE/产品/Build/CRC及原Modbus通信均已确认；
COM3后续10次通信检查全部通过，BLE和串口升级前后参数内容一致（仅采集时间变化）。
串口升级后BLE身份/实时数据复查通过；中途Windows GATT AccessDenied在用户断开其它上位机连接后消失。

首次串口传输在72.6%丢失应答并取消未提交会话；补齐与普通Modbus相同的20ms应答后转换间隔后，
还观测到擦除阶段QUERY丢失。客户端对擦除/校验阶段仅重试无副作用QUERY，保持阶段总超时，
不重发DATA/COMMIT。前两次失败均未执行COMMIT，修正后的同镜像升级成功。
现有Windows入口和CLI复用`Shared/TelinkSerialOta.cs`；CLI串口必须显式`--target telink`，
串口auto继续保留STM32 IAP选择。

本次完成四产品sources/link/resources、串口core/端口故障注入、UART所有权、SH休眠与禁休眠回归、
Windows串口/BLE OTA测试及客户版/内部版/CLI的net8/net48构建。实板仅D014开发测试板；
不代表其余产品、真实掉电、AFE故障修复、物理MOS保护或量产低功耗验收。

## 多轮升级修复与正式策略恢复

首次成功不能证明双向槽切换均正常。后续不同镜像测试发现：TC32使用`-fpack-struct -fshort-enums`，
状态结构的`base`偏移为7；把`&update.base`传给接收普通`uint32_t *`的端口时，
未对齐word写导致候选高槽`0x20000`地址回填错误，随后被Flash范围检查拒绝（error6，尚未擦除）。
改为4字节对齐的局部变量接收地址，再赋回成员；回归编译使用相同ABI并显式检查端口输出指针对齐。

修复后在同一D014开发板、COM3/115200上连续6轮全部成功：

| 轮次 | 运行版本切换 | 启动后镜像CRC | 含独立回读的用时 |
|---|---|---|---|
| 1 | U1 → U2 | 06c946e2 | 63.22秒 |
| 2 | U2 → U3 | 50ee35c5 | 60.95秒 |
| 3 | U3 → U1 | fca0d38b | 60.78秒 |
| 4 | U1 → U2 | 06c946e2 | 60.78秒 |
| 5 | U2 → U3 | 50ee35c5 | 60.89秒 |
| 6 | U3 → 原V1.0 | 54cb3e29 | 60.89秒 |

每包116180字节。数据阶段末速率约2.7 KiB/s，按速率估算传输约42秒；
表中时间另含擦除、完整校验、重启、参数采集和两次独立重新打开COM3核对，不是纯传输时间。
每轮同时核对FINISH读回CRC、COMMIT确认、新程序IDLE/产品/Build/CRC、不同运行版本和两次独立Modbus回读。
19组参数仅Software身份变化，最后恢复V1.0时全部业务参数与测试前一致。
每轮有2次只读QUERY超时被恢复，DATA/COMMIT等写命令超时为0；写结果未知时不自动重发。
这些镜像均基于`b5f6c48e-dirty`；U1/U2/U3仅临时修改运行版本字符串，无伪造Build ID或测试版本提交，
最终版本与临时诊断点均已还原。详细证据为用户输出目录中的`20261021-d014-repeat-ota/多轮串口OTA验证收据.json`。

恢复正常策略时补充D014通信域门禁：真实UART状态机模型先复现OTA静默期间断电，再验证
会话保持供电、结束后恢复断电，覆盖开发/生产及三个波特率。保护性强制休眠仍可中断升级，
不能承诺AFE故障板无限保持在线。正常PM恢复后完成四产品sources/link/resources、串口core/端口ABI与故障模型、
UART及SH休眠回归；未在本次整理时再次操作设备。

Windows串口与BLE协议回归通过，发布沿用唯一维护源`b2ca53c4`及其已存在的兼容修改，
在发布manifest、源码补丁和源码快照中记录工作树状态，内部版和CLI包含串口OTA，客户版继续保持原权限边界。

6轮连续成功不足以证明没有bug。尚未实测恢复正常PM后的D014光耦唤醒/上电时序、其余三产品实板、
真实拔线/低压/掉电/看门狗或保护性强制睡眠中断；双槽不提供应用异常自动回滚。
AFE通信故障仍未修复，不能据串口升级成功判定采样、MOS保护或量产验收通过。
