# D008 电流保护恢复验证记录

固件提交：`693b532640522ecfdfaa4844828fe495f8bc6ec0`，分支 `refactor/d008-common-bms-features`。

正式产物：`outputs/d008-current-recovery-20260917/D008_24S_LFP_SW1_HW1_CURRENT_RECOVERY_693b532.bin`。

**该 BIN 是当前源码默认 24S LFP / SW=1 / HW=1，不是历史 16S 台架版本。未烧录、未执行 OTA。**

- BIN 大小：106804 bytes。
- SHA-256：`53e4ab372df5f08a9e845951b2b29840413d02a80409338f4830a79a698e018e`。
- 全部 16 个 Host/contract 脚本通过，包含新增恢复执行测试。
- SW/HW=11、10、01、00 均通过 source-order、TC32 clean rebuild、check-fw、MAP、manifest、verify。
- cppcheck：0 error、0 warning，94 条 style；没有把 style 当作实板验证结果。
- 生产配置 text=102632、data=4016、bss=7172；Flash(text+data)=106648 bytes，slot=126976 bytes。
- 相比上一 ACC 验证生产配置，text +352、data +16、bss +16 bytes；恢复状态本身 20 bytes，无动态内存。
- 链接器保留 600-byte 栈下界检查通过；未实测最大中断嵌套/运行时栈高水位，不能据此承诺最坏栈余量。
- BIN 携带诊断 build ID 0x693b5326；manifest 校验 94 个对象及工具链链接输入通过。

实现与验收步骤见 [D008_CURRENT_RECOVERY.md](D008_CURRENT_RECOVERY.md)。放电恢复允许负载移除或可靠充电，充电过流至少30秒，软件三级与硬件共同受控；保持 AUTO_DIODE 续流和 MCU 复位原行为。本轮不修改 Windows EXE。旧上位机恢复参数字段尚不能表达固定恢复策略，以该文档为准。

`TODO_VERIFY_HW`：PB1 与实际输出关断时序、反向充电续流/恢复、过流重复动作、外部 Gate/Vgs、抖动、掉线、ACC 复位。实板验收未完成。
