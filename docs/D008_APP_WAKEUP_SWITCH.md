# 应用定时唤醒功耗对比开关

`conf.h` 中 `BMS_APP_SAMPLE_WAKEUP_ENABLE` 默认为 `1u`。设为 `0u` 后重新编译，关闭200 ms应用唤醒的注册/安排；启动显式取消该截止时间，ACC深睡眠和整机断电入口的取消调用保持有效。可用编译参数 `-DBMS_APP_SAMPLE_WAKEUP_ENABLE=0` 覆盖。

此开关不关闭BLE事件、GPIO唤醒或主循环采样逻辑，也不保证程序必定休眠。关闭后采样随其他唤醒/主循环机会运行，可能影响软件保护响应、SOC和按样本计数的滤波；仅用于受控功耗对比，生产保持1。不要以此代替修改BLE间隔或采样周期。

比较时保持BLE连接状态、广播参数、负载、AFE配置和LED状态相同。当前用户工作区有采样后翻转LED的测试代码，其频率也可能随采样机会变化，整板电流差不能全部归因于MCU唤醒开销。实测功耗和保护时序仍为TODO_VERIFY_HW。

## 构建验证

提交8fad72f；按当前16S工作区构建，保留原有LED、软件参数epoch=5及参数默认值改动。仅唤醒宏不同。未烧录，未实测功耗。

开/关两版均通过source-order、TC32 clean rebuild、check-fw、MAP、manifest、verify；cppcheck无error/warning；既有power/SOC Host测试通过。

- D008_16S_APP_WAKEUP_1_8fad72f.bin: SHA256 `2be8cc6bf213bee50fac0d6b92b7afdc6d4bf7d0d1fb98547db8836ee3edf927`

- D008_16S_APP_WAKEUP_0_8fad72f.bin: SHA256 `5ef8e02e2785c3f56996f6cb4e1d5aec92becf162d731f13a8a6c408ecef34c5`
