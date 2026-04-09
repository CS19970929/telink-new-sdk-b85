# BMSAssistantQt Windows 交付说明

## 交付定位

`BMSAssistantQt` 当前定位为 `Windows/macOS/Linux` 共用的一套 BLE 上位机。

Windows 侧的核心目标：

- 扫描并连接当前 `vendor/ble_sample` 固件广播出来的 BLE 设备
- 通过 `Telink SPP` 收发 `Modbus RTU over BLE`
- 查看电池状态页
- 执行调试工作台里的读写寄存器、原始帧、写 `SOC`、写 `0x1103`
- 导出报文日志与电池快照，便于售后与远程排查

## 当前 Windows 包内容

`package-windows.bat` 打出来的目录中，建议直接交付这些内容：

- `BMSAssistantQt/`
- `Launch-BMSAssistantQt.bat`
- `docs/README.md`
- `docs/WINDOWS-DELIVERY.md`

其中：

- `Launch-BMSAssistantQt.bat` 是客户侧推荐入口
- `BMSAssistantQt/BMSAssistantQt.exe` 是主程序

## 首次运行要求

### 1. 系统要求

- Windows 10/11
- 主机带 BLE 功能，或已接入支持 BLE 的蓝牙适配器
- 开发/打包机器需要 Python 3.9+，推荐 Python 3.10+；如果没有 `python` 命令，脚本会自动尝试 Windows `py -3` 启动器
- 脚本会把虚拟环境与打包中间目录放在 `%LOCALAPPDATA%\BMSAssistantQt\`，避免工程目录很深时触发 Windows 路径长度限制

### 2. 蓝牙要求

- 设备管理器中蓝牙适配器工作正常
- Windows 蓝牙功能已开启
- 不要让手机 App 长时间占着同一块板子

## 使用建议

### 1. 扫描

- 默认用 `全部设备`
- 先不要勾 `只显示疑似 BMS`
- 找不到名称时，优先看是否存在带 `180F / 1812` 的设备

### 2. 自动刷新

- `电池状态` 页默认开启 `自动刷新`
- 自动刷新只在 `电池状态` 页工作
- 切到 `调试工作台` 时，自动刷新会停掉，避免和手动调试命令互相抢链路

### 3. 配置持久化

程序会自动记住这些内容：

- 扫描模式
- 搜索关键字
- 是否只显示疑似 BMS
- 自动刷新开关与周期
- 当前页签
- 手动读写寄存器输入框
- 原始帧输入框
- 蓝牙名写入输入框

这部分通过 `QSettings` 持久化，Windows 下通常保存在当前用户配置区。

## 导出能力

### 1. 导出报文日志

适用场景：

- 客户反馈“偶现读不到数据”
- 需要把现场收发帧发回研发分析

导出结果：

- `CSV`

### 2. 导出电池快照

适用场景：

- 保存当前页面关键指标
- 保存当前寄存器块与原始响应

导出结果：

- `JSON`

## 建议交付前验证

至少完成一次 Windows 实机联调，确认：

1. 能扫描到 `BT_*` 设备
2. 能连接并进入 `READY`
3. 电池状态页自动刷新正常
4. 单次手动刷新正常
5. `写 SOC -> 0x1005` 正常
6. `写 0x1103 = 0x0003` 正常
7. 手动读寄存器正常
8. 报文日志导出正常
9. 电池快照导出正常

开发机还建议先执行：

```bat
scripts\run.bat
```

确认脚本能自动创建 `%LOCALAPPDATA%\BMSAssistantQt\venv`、安装 `PySide6/PyInstaller` 并拉起主窗口。正式打包再执行：

```bat
scripts\package-windows.bat
```

## 当前边界

- 这套 Qt 工程当前是桌面上位机，不是手机 App
- `iPhone/iPad` 仍建议走原生 `SwiftUI + CoreBluetooth`
- Windows 包当前具备打包路径，但正式对外发客户前，仍建议你在公司 Windows 机器上跑一轮完整实测
