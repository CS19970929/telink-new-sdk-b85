# AGENTS.md — BMS 客户端专用主线

## 定位与固定入口

- 本工作树用于 Windows 上位机、Android App 和配套 CLI，负责分支固定为 `client/main`。本机目录：`C:\Users\Administrator\source\bms-client`，源码在其 `bms-tool-windows/`。
- 仓库仍为 `CS19970929/telink-new-sdk-b85`；客户端基础适用于今后的多种 BMS 串口/BLE 项目，不限 Telink。其他电脑可有不同目录，但应使用同一负责分支。
- 开工先执行 `pwsh -NoProfile -File bms-tool-windows/check-client-worktree.ps1`，核对仓库、分支、HEAD 和 dirty 状态，并简短报告。分支不符时先定位已有 `client/main` 工作树，不从当前固件/旧客户端分支继续开发。
- 新聊天、新需求、新板型、编译、测试、安装包和客户/内部版本均不自动新建分支。未经本次明确授权，不得创建分支、复制长期客户端线，或用 detached HEAD 开发未归并新线。
- 此次是在已有客户端分支上收回有效进度后重命名；旧产品/功能分支保留历史，后续增量直接提交 `client/main`。不擅自删除旧分支、强推、重置用户工作或切换固件工作树。
- 现状、来源、平台边界及构建说明见 [统一客户端开发指南](bms-tool-windows/docs/CLIENT_MAIN.md)。GitHub 仓库的默认 `master` 并非客户端负责分支。

## 源码与修改边界

- `bms-tool-windows/BmsTool.Windows/`：客户 Windows 版。
- `bms-tool-windows/BmsFactoryTest.Windows/`：内部 Windows 版；工厂维修、故障注入等内部专用功能保持隔离。
- `bms-tool-windows/BmsTool.Android/`：同一套 Android App 的 Customer / Internal 构建变体。
- `bms-tool-windows/BmsTool.Cli/`：配套命令行工具。
- `bms-tool-windows/BmsTool.Core/` 与 `Shared/`：公共协议、模型、会话、升级等；Core 当前通过项目链接复用部分 Windows 源文件，不能误删为“平台重复文件”。
- 默认只改客户端、相关测试/文档和客户端 CI。仓库携带的 SDK、固件、Boot、链接脚本与预编译库不属于本分支日常开发范围；不要整体合并固件分支来获取客户端改动。
- 先按目标 BMS 的实际协议与能力适配。不得借客户端统一改变寄存器、缩放、字节序、Flash/EEPROM 布局、升级格式或 Boot/App 边界。不存在的串位保持 `61001`，不计入有效串数。
- 新项目先扩展已有产品识别、能力检查、参数和界面，复用串口/BLE 传输及公共实现；没有实际需要时不引入新抽象或另一套客户端。
- `ota-app/TelinkOta.*`、`android-bms-app`、`bms-tool-android`、SDK `BMSAssistant*` 和发布 EXE/APK 不是源码入口。

## 行为与版本边界

- 客户版与内部版共用公共功能，保留现有服务授权与专用功能限制。统一分支不代表所有平台已具备相同专用页面。
- 参数候选必须绑定设备连接及有效读回；切换、断开、重连后重新读取，旧异步结果不得发布到新连接。保留整组事务、部分提交提示、失败后重读规则，禁止盲目重发写入。
- 升级按有效能力、产品、地址、容量和包校验分流；明确区分标准 YMODEM、旧页协议、旧 HS-D015 和 Telink 传输。无法确认身份/边界时拒绝猜测。
- OTA 导入不授权自动选择陌生设备、升级、烧录或改固件。Android 自动发送只使用已经明确选择的端点；CLI 实板操作保留原参数授权和 JSON/exit-code 合同。
- 本轮保留 Android `0.9.1` 的既有版本字段；本地编译不等于重新发布旧版本安装包。正式发布按发布流程记录新提交、版本和产物哈希。

## 验证、提交与临时文件

- 公共客户端修改后构建客户 WPF、内部 WPF 和 CLI；影响 Core 或 Android 的改动还需构建 Android 两版。按修改范围运行现有相关回归，不自动运行实板脚本。
- 构建、测试、缓存、截图及中间文件全部写到用户临时区；例如 `$env:LOCALAPPDATA\CodexTemp\bms-client\<task-id>`。使用 `--artifacts-path`，禁止在工作树内创建临时目录或生成 `bin/obj` 缓存。
- 客户端任务不执行仓库历史 `bms_tools/bms.py build/rebuild/ci` 或 VS Code 发送固件任务；这些会构建或发送固件。用户未要求时不生成固件 BIN、不烧录、不安装。
- 测试结果区分源码/host、桌面/App 编译、真实传输、实板行为；不能把 fake transport 或 APK 编译当作手机和 BMS 验收。
- 保留无关 dirty 改动。较大变更分阶段提交，只提交本次相关文件；交付报告实际分支、提交、验证及是否新增分支。
