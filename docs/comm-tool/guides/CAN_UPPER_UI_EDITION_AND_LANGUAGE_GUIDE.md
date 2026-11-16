# CAN 上位机版本与语言交付指南

> 固定来源说明：本文来自comm-tool-branch@5521e851，保留当时的行为记录。当前目录、Keil工程、双协议合同及验证范围见[当前入口](../README.md)。


文档状态：CURRENT

最后更新时间：2026-09-09

适用源码：`tools/comm_tool_upgrade_ui.py`、`tools/comm_tool_upgrade_ui_i18n.py`

## 1. 交付矩阵

四个上位机由同一套源码和同一个 PyInstaller spec 生成，CAN ID、寄存器地址、字段单位、字节序和升级流程完全一致。

| 使用对象 | 语言 | 固定文件名 | 启动时可见页面 |
|---|---|---|---|
| 客户 | 中文 | `dist/BMS_CommTool_Upgrade_UI.exe` | 实时监控、存储信息 |
| 客户 | English | `dist/BMS_CommTool_Upgrade_UI_EN.exe` | Live Monitor、Event Storage |
| 内部 | 中文 | `dist/BMS_CommTool_Upgrade_UI_Internal.exe` | 全部六页 |
| 内部 | English | `dist/BMS_CommTool_Upgrade_UI_Internal_EN.exe` | All six tabs |

程序根据 EXE 文件名选择版本和语言：文件名包含 `Internal` 时为内部版，以 `_EN` 结尾时为英文版。发布时不得修改这四个固定名称，否则可能选错版本或语言。

## 2. 客户版维护入口

客户版默认隐藏以下四页：

- 实时数据 / Live Data；
- 参数设置 / Parameters；
- 系统状态 / System Status；
- 其它功能 / Maintenance。

底部“维护解锁 / Unlock Maintenance”输入默认密码 `hs456` 后，上述页面按原顺序恢复。点击“锁定维护功能 / Lock Maintenance”可立即隐藏；每次重新启动都恢复锁定状态，密码和解锁状态不会写入配置文件。

该密码仅用于减少普通客户误操作，是本地 UI 访问分级。密码存在程序源码和可执行文件中，不具备抗逆向能力，也不会阻止第三方按公开协议直接访问 BMS。若未来需要真正的权限控制，应在 BMS 固件中设计挑战应答、权限级别和受保护写命令，并同步升级协议版本；不能只增强界面密码框。

## 3. 内部版

内部版启动后显示：实时监控、实时数据、存储信息、参数设置、系统状态、其它功能。内部版没有维护解锁按钮，适用于研发、生产和售后诊断。内部版不得作为默认客户交付件。

## 4. 英文版规则

英文版通过 `comm_tool_upgrade_ui_i18n.py` 在控件创建和状态更新时翻译可见文本。变量名、函数名、CAN/Modbus 字段名、数值、单位计算和协议报文不做翻译。文件路径、串口名、序列号、版本号以及设备返回的原始内容保持原样。

新增或修改 UI 文案时：

1. 中文源码继续作为界面文案基准；
2. 在 `_EXACT_EN` 中加入完整、优先匹配的英文文案；
3. 仅对通用短语使用 `_REPLACE_EN`，避免替换顺序产生混合语言；
4. 同时运行中文和英文 `--self-test`，确保页签、按钮、状态变量、下拉选项及表格标题无中文残留；
5. 不翻译协议常量、设备数据和用户选择的文件路径。

## 5. 构建

在仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_comm_tool_upgrade_ui_exe.ps1 -Variant All
```

也可用 `-Variant CustomerZh`、`CustomerEn`、`InternalZh` 或 `InternalEn` 单独构建。PyInstaller 中间文件写入 `%LOCALAPPDATA%\CodexTemp\103-309\build-comm-tool-ui`，不会污染 Git 工作树；正式 EXE 写入 `dist`。

源码自检：

```powershell
py -3.9 tools\comm_tool_upgrade_ui.py --self-test --edition internal --language zh
py -3.9 tools\comm_tool_upgrade_ui.py --self-test --edition customer --language en
```

## 6. 发布验收

- 客户中文、客户英文启动时都只有两个页面；内部中文、内部英文启动时都有六个页面。
- 客户版输入错误密码不解锁；输入 `hs456` 显示全部六页；重新锁定和重新启动后回到两页。
- 中英文版本的连接、实时监控、日志保存、参数读写和升级结果一致。
- 英文版操作文案为英文，用户文件路径和设备原始值不被修改。
- 四个 EXE 均能启动，客户中文版实际覆盖原 `dist/BMS_CommTool_Upgrade_UI.exe`。
- 发布包只放入对应客户所需语言的客户版；内部版单独管控。
