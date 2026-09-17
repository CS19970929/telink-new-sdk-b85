# 正常启动 AFE1 误报修复

问题：公共 bms_afe_guard 在前两次成功采样时，为等待三次连续有效采样仍无条件raise AFE1。此时后台通信成功，错误由guard主动制造。错误计数/事件采样若落在该窗口，会留下用户可见记录。不是配置readback阈值过严的证据。

修改：将comm_inhibit与comm_fault_latched区分。健康启动前两次仍禁止输出，诊断继续显示通信资格未完成，但不raise AFE1。初始化实际失败、无效采样和命令失败仍走原报错/关输出路径；真实错误恢复期间继续保持AFE1，三次有效采样后才释放门禁。两次通信失败后停总线等待硬件watchdog的策略和时长不变。后端配置、I2C重试和readback校验没有放宽。

本次不清空历史事件，不屏蔽真实故障，不改变软件/硬件参数、Flash布局或revision。旧AFE1历史记录仍会存在；验收应比较升级后新增事件。

验证：`python tests/d008_power_soc_host_check.py` 的guard夹具执行真实guard源码。新增健康启动零人为AFE1且前两次输出OFF、第三次可输出；真实read失败立即报错并需三次有效采样恢复；两次失败后的25采样周期总线静默；backend init/config失败仍报错。相同夹具运行旧代码会在健康启动断言处失败。

证据边界：本次确认源码存在必然的启动资格误报路径，并有Host复现。未对实板每条既有AFE1日志证明其来源；若更新后仍有新增AFE1，应继续查真实初始化/读写失败，不能以通信稍后恢复为由忽略。实板复位与事件增量验证保持TODO_VERIFY_HW。

## 验证与产物

2026-09-17，代码提交e683c06b，原refactor/d008-common-bms-features分支。source-order 94 objects、15项Host/contract及22项tooling unittest通过。TC32 clean build/check-fw/MAP/manifest/verify覆盖SW/HW=1/1、1/0、0/1、0/0和当前16S SW0/HW1。cppcheck应用覆盖31单元、缺口0，0 error/warning、94 style，MISRA未执行。

默认1/1 text=101816、data=4000、bss=7148 bytes，相对上一版增加Flash 80bytes，链接静态RAM总量不变；未做实板栈高水位测量。

交付：outputs/d008-afe-startup-20260917/D008_16S_SW0_HW1_AFE_STARTUP.bin。保留用户当前16S SW0/HW1台架设置、欠压3000/3000/2200/3100mV及其余既有工作区改动；参数revision不变。精确配置差异、manifest、MAP、ELF和测试日志在evidence。无需换上位机。未自动烧录/OTA，也未推送远端。

验收：升级后多次MCU复位，对比新增AFE1事件/重复计数。健康启动应仍完成3次采样资格确认，但不再由guard产生AFE1；实际断开AFE或注入通信故障仍应告警并阻断输出。后者仅在受控台架手动验收，本次没有进行设备故障注入。
