# 应用定时唤醒功耗对比开关

`conf.h` 中 `BMS_APP_SAMPLE_WAKEUP_ENABLE` 默认为 `1u`。设为 `0u` 后重新编译，关闭正常状态下200 ms应用唤醒的安排；启动仍注册轻量回调，无待恢复故障时取消该截止时间，ACC深睡眠和整机断电入口的取消调用保持有效。可用编译参数 `-DBMS_APP_SAMPLE_WAKEUP_ENABLE=0` 覆盖。

此开关不关闭BLE事件、GPIO唤醒或主循环采样逻辑，也不保证程序必定休眠。关闭后采样随其他唤醒/主循环机会运行，可能影响软件保护响应、SOC和按样本计数的滤波；仅用于受控功耗对比，生产保持1。不要以此代替修改BLE间隔或采样周期。

比较时保持BLE连接状态、广播参数、负载、AFE配置和LED状态相同。当前用户工作区有采样后翻转LED的测试代码，其频率也可能随采样机会变化，整板电流差不能全部归因于MCU唤醒开销。实测功耗和保护时序仍为TODO_VERIFY_HW。

## 构建验证

提交8fad72f；按当前16S工作区构建，保留原有LED、软件参数epoch=5及参数默认值改动。仅唤醒宏不同。未烧录，未实测功耗。

开/关两版均通过source-order、TC32 clean rebuild、check-fw、MAP、manifest、verify；cppcheck无error/warning；既有power/SOC Host测试通过。

- D008_16S_APP_WAKEUP_1_8fad72f.bin: SHA256 `2be8cc6bf213bee50fac0d6b92b7afdc6d4bf7d0d1fb98547db8836ee3edf927`

- D008_16S_APP_WAKEUP_0_8fad72f.bin: SHA256 `5ef8e02e2785c3f56996f6cb4e1d5aec92becf162d731f13a8a6c408ecef34c5`

## suspend 期间故障恢复修正

关闭定时唤醒后，BLE广播等事件可能只提供800 ms间隔的采样；过流/短路恢复为了防止陈旧样本误判，会在间隔大于400 ms时重置200 ms连续证据。于是即使PB1已经为高，资格确认也可能一直重新开始；连接BLE缩短间隔才恢复。

现在宏0仅取消正常运行的固定周期唤醒：D008充电过流或放电过流/短路锁存期间，临时维持200 ms应用唤醒。负载移除/可靠充电/30秒恢复仍走原有判定和clear/readback；锁存解除后自动取消临时唤醒。没有修改保护条件、去抖时间、PB1极性或增加GPIO唤醒。等待故障恢复期间功耗会高于无故障suspend，这是保证独立于BLE恢复的代价。MCU复位及ACC深睡眠保持既有语义。

新增RAM只读接口 `bms_afe_current_recovery_pending()`；不访问AFE，不发MOS命令。Host测试复现800 ms不断重置，并验证200 ms恢复、宏0/1调度、解除后取消以及tick回绕；仍需实板确认功耗和无BLE恢复时序。
