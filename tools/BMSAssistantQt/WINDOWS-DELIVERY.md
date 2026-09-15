# BMSAssistantQt Windows 编译与交付说明

## 1. 当前工程

Windows 桌面上位机实际工程：

```text
tools/BMSAssistantQt
```

技术栈：Python + PySide6 + QtBluetooth + QtWidgets；使用 PyInstaller 生成 Windows 目录包。

当前功能：BLE扫描/连接、Telink SPP、Modbus RTU over BLE、电池状态、软件保护参数预览、手动寄存器/原始帧、BT name suffix、CSV/JSON导出。

当前边界：**没有 direct-serial transport，也没有专用 AFE Hardware Protection V2 编辑器。**

## 2. 从 GitHub 拉对应产品

### D008

```bat
git clone --single-branch --branch feature/sh3673510-d013-bmsdvc https://github.com/CS19970929/telink-new-sdk-b85.git D008-BMS
cd /d D008-BMS\tools\BMSAssistantQt
```

### D011

```bat
git clone --single-branch --branch feature/sh3673510-d011-bms https://github.com/CS19970929/telink-new-sdk-b85.git D011-BMS
cd /d D011-BMS\tools\BMSAssistantQt
```

### D013

```bat
git clone --single-branch --branch feature/sh3673510-d013-bms https://github.com/CS19970929/telink-new-sdk-b85.git D013-BMS
cd /d D013-BMS\tools\BMSAssistantQt
```

## 3. 打包前必须核对串数

文件：

```text
bmsassistantqt\protocol.py
```

字段：

```python
RegisterCatalog.currentProjectSeriesCount
```

产品值：

| 产品 | 值 |
|---|---:|
| D008 24S LFP | 24 |
| D008 20S NMC | 20 |
| D011 | 10 |
| D013 当前代码 profile | 4 |

当前 Qt 工具还没有从设备 metadata 自动读取串数，因此 D008/D013 打包前必须人工确认该值；否则单体显示数量错误。

## 4. 开发运行

要求：Windows 10/11、BLE适配器、Python 3.9+。

直接执行：

```bat
scripts\run.bat
```

脚本自动：

```text
查找 python 或 py -3
创建 %LOCALAPPDATA%\BMSAssistantQt\venv
升级 pip
安装 requirements.txt
启动 main.py
```

当前依赖：

```text
PySide6>=6.8,<7
PyInstaller>=6.10,<7
```

## 5. 正式打包

```bat
scripts\package-windows.bat
```

脚本会先执行：

```bat
python main.py --smoke-test
```

再调用 PyInstaller：

```text
--windowed
--name BMSAssistantQt
--collect-all PySide6
--hidden-import PySide6.QtBluetooth
```

虚拟环境、build和PyInstaller中间目录放在：

```text
%LOCALAPPDATA%\BMSAssistantQt\
```

避免深目录触发传统 Windows MAX_PATH 问题。

## 6. 生成结果

项目目录下：

```text
.dist\BMSAssistantQt\BMSAssistantQt.exe
.dist\Launch-BMSAssistantQt.bat
.dist\docs\README.md
.dist\docs\WINDOWS-DELIVERY.md
```

客户/测试人员优先运行：

```text
Launch-BMSAssistantQt.bat
```

## 7. 建议交付文件名

```text
BMSAssistantQt_HS-D008_24S-LFP_<YYYYMMDD>_<shortsha>.zip
BMSAssistantQt_HS-D008_20S-NMC_<YYYYMMDD>_<shortsha>.zip
BMSAssistantQt_HS-D011_10S_<YYYYMMDD>_<shortsha>.zip
BMSAssistantQt_D013_CODEPROFILE_4S_<YYYYMMDD>_<shortsha>.zip
```

D013 原理图/BOM确认后，再把 `CODEPROFILE` 改成真实硬件标识。

获取 short SHA：

```bat
git rev-parse --short HEAD
```

## 8. 交付前最小验证

1. `git branch --show-current` 确认产品分支；
2. `git rev-parse HEAD` 记录源码版本；
3. 核对 `currentProjectSeriesCount`；
4. `scripts\run.bat` 能启动；
5. `scripts\package-windows.bat` 成功；
6. 用生成包重新启动；
7. 实机扫描、连接到目标BMS；
8. 电池状态刷新、手动读寄存器、日志/快照导出正常；
9. 不使用普通单寄存器UI绕过 AFE Hardware Protection V2 的35-word事务。

## 9. BLE协议限制

当前默认 ATT MTU=23，安全单请求20 byte；response支持分片重组，request没有通用大包重组。因此普通 BLE Modbus 0x10 建议最多5 words。

AFE Hardware Protection V2 要求完整35-word原子写，当前 Qt BLE 工具不具备这条正式写路径。