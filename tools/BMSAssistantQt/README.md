# BMSAssistantQt

`BMSAssistantQt` 是当前仓库实际存在的桌面 BLE 上位机，技术栈：

```text
Python 3.9+
PySide6 >=6.8,<7
QtBluetooth
QtWidgets
PyInstaller >=6.10,<7
```

主要能力：BLE扫描/连接、Telink SPP、Modbus RTU over BLE、电池状态、保护参数预览、手动读写寄存器、原始帧、BT name suffix、CSV/JSON导出。

> 当前没有 direct-serial transport，也没有专用 AFE Hardware Protection V2 编辑器。完整35-word AFE HW profile不能通过当前BLE单包路径安全提交。

## 1. 从 GitHub 拉代码

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

## 2. 先确认上位机串数

文件：

```text
bmsassistantqt/protocol.py
```

当前代码：

```python
class RegisterCatalog:
    currentProjectSeriesCount = 10
```

按产品修改：

| 分支/产品 | 值 |
|---|---:|
| D008 24S LFP | 24 |
| D008 20S NMC | 20 |
| D011 | 10 |
| D013 当前代码profile | 4 |

当前工具还没有从设备metadata自动读取串数，所以 D008/D013 打包前必须核对这个值。

## 3. Windows开发直跑

```bat
scripts\run.bat
```

脚本自动：

1. 查找 `python`，失败再尝试 `py -3`；
2. 在 `%LOCALAPPDATA%\BMSAssistantQt\venv` 创建虚拟环境；
3. 安装 `requirements.txt`；
4. 启动 `main.py`。

依赖文件：

```text
PySide6>=6.8,<7
PyInstaller>=6.10,<7
```

## 4. Windows打包

```bat
scripts\package-windows.bat
```

脚本先运行：

```bat
python main.py --smoke-test
```

然后使用 PyInstaller：

```text
--windowed
--name BMSAssistantQt
--collect-all PySide6
--hidden-import PySide6.QtBluetooth
```

输出：

```text
.dist\BMSAssistantQt\BMSAssistantQt.exe
.dist\Launch-BMSAssistantQt.bat
.dist\docs\README.md
.dist\docs\WINDOWS-DELIVERY.md
```

客户侧推荐入口：

```text
Launch-BMSAssistantQt.bat
```

## 5. macOS

```bash
cd <repo>/tools/BMSAssistantQt
./scripts/package-macos.sh
./scripts/run-macos-app.sh
```

输出：

```text
.dist/BMSAssistantQt.app
```

macOS BLE扫描需要 App 蓝牙权限；首次运行在“系统设置 -> 隐私与安全性 -> 蓝牙”授权。

## 6. Linux

```bash
cd <repo>/tools/BMSAssistantQt
./scripts/run.sh
./scripts/package-linux.sh
```

输出：

```text
.dist/BMSAssistantQt
```

## 7. BLE/协议边界

```text
Service    6E400001-B5A3-F393-E0A9-E50E24DCCA9E
Request    6E400002-B5A3-F393-E0A9-E50E24DCCA9E
Response   6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

默认 ATT MTU=23，当前安全单请求上限20 byte。大响应由客户端分片重组；request端没有通用大包reassembly。

因此普通BLE `0x10` 写多寄存器建议不超过5 words。AFE Hardware Protection V2要求完整35-word原子写，当前BLE Qt工具不具备这一安全路径。

## 8. 协议资产

共享协议资料：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/docs/register_catalog.json
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/docs/protocol_test_vectors.json
```

生成工具：

```bash
python3 tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/script/bms_client_asset_tool.py all
```

`generated/` 文件不要直接手改。注意当前 `register_catalog.json` 仍含部分历史 10S 描述；D008/D013 产品显示以对应分支固件源码和 `currentProjectSeriesCount` 为准，修改协议资产时要同步清理这些产品相关常量。

## 9. 建议交付包名

```text
BMSAssistantQt_HS-D008_24S-LFP_<date>_<shortsha>.zip
BMSAssistantQt_HS-D008_20S-NMC_<date>_<shortsha>.zip
BMSAssistantQt_HS-D011_10S_<date>_<shortsha>.zip
BMSAssistantQt_D013_CODEPROFILE_4S_<date>_<shortsha>.zip
```

D013硬件资料确认后再把 `CODEPROFILE` 去掉。
