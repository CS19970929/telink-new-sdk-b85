# 保护分组及批量写入验证记录

固件提交 `f94d93c66ffc71659f20da6c3da2cda6de9ffdba`；Windows提交 `125498f`。源码改动说明见 [D008_PROTECTION_GROUPS.md](D008_PROTECTION_GROUPS.md)。

- 17项Host/contract全部通过；独立温度测试执行生产算法，不只检查文本。
- TC32 clean build：SW/HW/TEMP=111、101、011、001、110、100、010、000，以及本地16S工作区均通过source-order、rebuild、check-fw、MAP、manifest、verify。
- cppcheck：0 error/warning，95条style。生产111 text=102824，data=4016，bss=7172；相较上轮增加text192 bytes，RAM统计不变。链接栈保留断言通过，未测运行时栈高水位。
- Windows客户版/内部测试版均发布成功，标签 `protection-batch-20260917`；批量写入host测试和AFE分片/串口回归通过。旧net7框架NETSDK1138警告仍存在，未在本次升级框架。

固件产物位于 `outputs/d008-protection-groups-20260917/`：
- `D008_24S_LFP_SW1_HW1_TEMP1_f94d93c.bin`：106996 bytes，SHA256 `3733529b8e33dc9056f9226eb48cfe492ea074af63372f0d8ecfb067dacea396`。
- `D008_16S_WORKSPACE_SW1_HW1_TEMP1_f94d93c.bin`：107044 bytes，SHA256 `ef0d6de01cab8ad3426467596f044266530cf32c71870596cd7e47d11c96fe04`。

24S为提交默认配置；16S为用户当前工作区构建，包含原有LED改动、软件参数epoch=5、CUV_filter3=100等，输入差异保存在16S-workspace-input.patch，未带入源码提交。刷写16S版本时，软件参数revision不匹配会按既有升级规则更新软件默认参数；本次没有新增或提升该epoch。

用户现有dirty文件全部保留。没有连接设备写参数、烧录、OTA或做故障注入；温度/NTC/MOS实板触发与真实批量通信仍为TODO_VERIFY_HW。
