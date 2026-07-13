# 测试工具和搭建

## 已使用的软件

- TC32 GCC 工具链：`C:\TelinkSDK\opt\tc32\bin`，包括 gcc、objdump、objcopy、nm、readelf、size。
- Make：`C:\qp\qtools\bin\make.exe`。
- Telink 后处理：工程自带 `tl_check_fw2.exe`。
- PowerShell、Python 3、Git、`unittest`、CRC/Manifest/损坏/布局脚本。
- 当前工程未发现可直接复现的 Cppcheck/MISRA 配置；静态分析表中必须保留“待执行”，不能标成通过。

建议补充当前 IDE 的精确版本、Telink 下载工具版本、串口终端、BLE 调试工具和二进制编辑器，并记录安装包哈希或版本截图。

## 硬件与接线

需要 Telink BMS 样板、Telink 下载器、隔离可调电源、电池/单体模拟器、电子负载、万用表、示波器、逻辑分析仪、串口工具、BLE 主机、MOS 反馈夹具、AFE I2C 断线开关、ADC 开短路/基准夹具。首次上电应限流，不连接真实大容量电池或高功率负载。

最低观测点：PA1/CTLC、PB6/AFE_CTL、PD4/RF_EN、CHG/DSG/PCHG 栅极或 AFE 控制信号、均衡输出、看门狗复位、Timer0 打点、SDA/SCL。所有地参考、探头衰减和触发条件必须写入报告。

## 软件步骤

1. 记录提交、分支、工具版本和测试宏。
2. 运行一键生产构建并保存 ELF、MAP、LST、BIN、两个 JSON 报告和 SHA-256。
3. 运行主机测试；确认生产 Manifest `test_build=false`。
4. 每次只启用一个故障掩码，生成独立测试镜像并立即改名隔离。
5. 烧录、读回并校验；上电前将负载置安全状态。
6. 同步采集诊断窗口、串口/BLE、逻辑分析仪和输出波形。
7. 完成测试后重刷生产镜像并再次验证输出门控。

任何未执行的硬件步骤均填写“未执行”，不得填写估计时间作为实测值。
