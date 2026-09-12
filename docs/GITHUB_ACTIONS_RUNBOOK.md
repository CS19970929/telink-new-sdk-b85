# GitHub Actions Windows TC32 Runner 完整运维手册

本文档是本仓库 GitHub Actions 云端编译的部署、使用、维护和故障恢复手册。
工作流实际行为以 .github/workflows/bms-ci.yml 为第一可信来源；本文档解释配置
原因、日常使用方法、安全边界和恢复步骤。

本文覆盖：

- GitHub-hosted 主机契约检查；
- 当前 Windows PC 上的 Telink TC32 self-hosted runner；
- Runner 首次安装、服务化、启停和重新注册；
- 手动触发、查看结果和下载 Artifact；
- public repository 安全边界；
- 常见故障和当前验证基线。

本文不证明固件已经通过实板验证。编译、静态分析、BIN 完整性、烧录和实板验证
是不同门禁，不能互相替代。

## 1. CI 架构

本仓库采用两层 CI。

### 1.1 Host contract checks

运行环境为 GitHub-hosted ubuntu-latest，不依赖本机在线，也不使用 TC32 编译器。
它快速检查可跨平台验证的源码和工具契约：

~~~text
python bms_tools/bms.py sources --check
python -m unittest tests.test_bms_tools -v
python tests/dvc1124_config_quick_check.py
python tests/flash_quick_check.py
python -m py_compile ...
~~~

这个 job 适合作为所有 PR 的基础 required check，但不能称为“生产固件编译通过”。

### 1.2 TC32 production build

运行环境为当前 Windows PC 上的 repository-level self-hosted runner。它使用项目
锁定的 Telink TC32 编译器、官方预编译库、链接脚本和固件后处理工具生成生产
固件。

执行顺序为：

~~~text
python bms_tools/bms.py env
python -m unittest tests.test_bms_tools -v
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static --no-report
python tests/dvc1124_config_quick_check.py
python tests/flash_quick_check.py
~~~

CI 使用 static --no-report，避免依赖当前开发电脑上的 Excel 报告模板。Cppcheck
机器可读结果仍会保留在 Artifact 中。

## 2. 触发规则和安全矩阵

| 事件 | Host contract checks | TC32 production build | 说明 |
|---|---:|---:|---|
| 推送到 refactor/bms-template-phase1 | 运行 | 变量启用时运行 | 正常集成路径 |
| 同仓库 PR 指向目标分支 | 运行 | 变量启用时运行 | 仅执行可信仓库分支 |
| 外部 fork PR | 运行 | 禁止运行 | 不允许外部代码进入本机 |
| workflow_dispatch | 运行 | 变量启用时运行 | 手动发布或复验 |

TC32 job 必须同时满足：

1. 仓库 Actions variable TELINK_TC32_CI_ENABLED 等于 1；
2. Runner 同时具有 self-hosted、Windows、X64、telink-tc32 标签；
3. PR 事件的 head repository 与当前 repository 相同。

不得删除外部 fork 判断，也不得把 TC32 job 改成 pull_request_target。仓库为 public，
外部贡献者可以控制 fork PR 中的代码；让这些代码在 self-hosted runner 上运行，
等价于允许其在当前 Windows PC 上执行命令。

## 3. 当前已部署基线

以下是 2026-09-12 在当前 Windows PC 上完成并验证的配置快照。

| 项目 | 当前值 |
|---|---|
| Repository | https://github.com/CS19970929/telink-new-sdk-b85 |
| Branch | refactor/bms-template-phase1 |
| Runner version | 2.337.0 |
| Runner directory | C:/actions-runner |
| Work directory | C:/actions-runner/_work |
| Runner name | DESKTOP-UU5VC9R-telink-tc32 |
| Runner group | Default |
| Runner labels | self-hosted, Windows, X64, telink-tc32 |
| Windows service | actions.runner.CS19970929-telink-new-sdk-b85.DESKTOP-UU5VC9R-telink-tc32 |
| Service account | NT AUTHORITY\NETWORK SERVICE |
| Startup | Automatic, delayed start |
| Shared Python | C:/Tools/Python312, Python 3.12.10 |
| Windows PowerShell 5.1 policy | LocalMachine=RemoteSigned |
| TC32 compiler | C:/TelinkIoTStudio/opt/tc32/bin/tc32-elf-gcc.exe |
| TC32 version | 4.5.1-tc32-1.3 |
| Cppcheck | C:/Program Files/cppcheck/cppcheck.exe |
| Repository variable | TELINK_TC32_CI_ENABLED=1 |

本机原有 C:/actions-runner-bms 属于另一个仓库和 Keil Runner。维护本项目 Runner 时
不得停止、覆盖、删除或重新注册该目录及其服务。

## 4. 首次安装或重建 Runner

### 4.1 安装前检查

在仓库根目录运行：

~~~powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python -m unittest tests.test_bms_tools -v
python tests/dvc1124_config_quick_check.py
python tests/flash_quick_check.py
~~~

至少确认：

- Python 可执行；
- C:/qp/qtools/bin/make.exe 或 PATH 中的 GNU Make 可用；
- TC32 编译器版本为 4.5.1-tc32-1.3；
- C:/Program Files/cppcheck/cppcheck.exe 可用；
- tl_check_fw2.exe、boot.link 和两个官方 .a 文件存在；
- source_order.txt 检查通过。

不要使用 ARM GCC、host GCC 或其他 TC32 版本替代正式工具链。

### 4.2 准备共享 Python

Runner 服务使用 NETWORK SERVICE，不应依赖 Administrator 的用户级 PATH 或
AppData 权限。当前机器把可供服务使用的 Python 放在：

~~~text
C:/Tools/Python312/python.exe
~~~

推荐安装一份所有用户可读执行的 Python 3.11+。共享 Python 必须先验证标准库：

~~~powershell
C:/Tools/Python312/python.exe --version
C:/Tools/Python312/python.exe -c "import argparse,csv,hashlib,json,subprocess,tempfile,unittest,xml.etree.ElementTree; print('stdlib-ok')"
~~~

把共享 Python 目录加入 machine PATH，避免使用 setx 导致长 PATH 截断：

~~~powershell
$pythonDir = 'C:/Tools/Python312'
$machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
$parts = @($machinePath -split ';' | Where-Object { $_ -and $_.Trim() })
$normalizedPythonDir = $pythonDir.Replace('/', '\').TrimEnd('\')
if (-not ($parts | Where-Object { $_.Replace('/', '\').TrimEnd('\') -ieq $normalizedPythonDir })) {
    [Environment]::SetEnvironmentVariable('Path', $pythonDir + ';' + ($parts -join ';'), 'Machine')
}
~~~

修改 machine PATH 后必须重启 Runner 服务，已有服务进程不会自动获得新环境。

### 4.3 Windows PowerShell 执行策略

GitHub Runner 的 powershell shell 会生成临时 .ps1 文件。NETWORK SERVICE 在
机器级策略未配置时可能回落到 Restricted。当前基线使用 RemoteSigned，不使用
长期 Bypass：

~~~powershell
C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Set-ExecutionPolicy -Scope LocalMachine -ExecutionPolicy RemoteSigned -Force"
C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -Command "Get-ExecutionPolicy -Scope LocalMachine"
~~~

预期输出为 RemoteSigned。若电脑受域策略管理，MachinePolicy 或 UserPolicy 可能
覆盖本地配置，应由管理员按组织策略处理，不能静默改成 Bypass。

### 4.4 下载并校验 Runner

在管理员 PowerShell 中执行。版本升级时应从 GitHub 官方 Add new self-hosted
runner 页面取得当前下载地址和 SHA-256，不要复用旧安装包。

当前验证版本：

~~~powershell
$runnerVersion = '2.337.0'
$runnerZip = "actions-runner-win-x64-$runnerVersion.zip"
$runnerUrl = "https://github.com/actions/runner/releases/download/v$runnerVersion/$runnerZip"
$expectedSha256 = '1150692afa94e71f872017e254ea55b6eece1eece3fe7e3a6d4c93d0a1b85cfc'

New-Item -ItemType Directory -Path C:/actions-runner -Force | Out-Null
Set-Location C:/actions-runner
Invoke-WebRequest -Uri $runnerUrl -OutFile $runnerZip

$actualSha256 = (Get-FileHash -LiteralPath $runnerZip -Algorithm SHA256).Hash
if ($actualSha256 -ne $expectedSha256) {
    throw 'GitHub Actions Runner checksum mismatch'
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $PWD $runnerZip), $PWD)
~~~

不得把下载 ZIP、截图或临时日志放入 Git 工作树。

### 4.5 取得一次性注册 token

在 GitHub 仓库中打开：

Settings → Actions → Runners → New self-hosted runner → Windows → x64

点击 Configure 命令右侧的 Copy。注册 token 是短时、一次性敏感信息：

- 不写入仓库、脚本、日志或文档；
- 不发送到聊天、Issue 或 PR；
- 不把网页渲染后的 Markdown 链接当成 URL；
- token 失效或暴露后立即刷新页面生成新的 token。

执行前应确认 URL 是普通的 https://github.com/...，不能含有方括号、圆括号或
Markdown 链接语法，并确认 token 与后续参数之间有空格。

### 4.6 注册为 Windows 服务

把 GitHub 页面复制的一次性 token 代入下列占位符，只在当前管理员 PowerShell
进程中使用：

~~~powershell
Set-Location C:/actions-runner
./config.cmd --unattended --url https://github.com/CS19970929/telink-new-sdk-b85 --token {FRESH_REGISTRATION_TOKEN} --name DESKTOP-UU5VC9R-telink-tc32 --labels telink-tc32 --work _work --runasservice --windowslogonaccount 'NT AUTHORITY\NETWORK SERVICE'
~~~

预期关键输出：

~~~text
Connected to GitHub
Runner successfully added
Settings Saved
Service ... successfully installed
Service ... started successfully
~~~

注册完成后清空剪贴板时要写入一个空格，Windows PowerShell 的 Set-Clipboard
不能可靠接受空字符串：

~~~powershell
Set-Clipboard -Value ' '
~~~

### 4.7 启用仓库变量

~~~powershell
gh variable set TELINK_TC32_CI_ENABLED --body 1 --repo CS19970929/telink-new-sdk-b85
gh variable get TELINK_TC32_CI_ENABLED --repo CS19970929/telink-new-sdk-b85
~~~

预期值为 1。

## 5. 安装后验收

### 5.1 服务状态

~~~powershell
$serviceName = 'actions.runner.CS19970929-telink-new-sdk-b85.DESKTOP-UU5VC9R-telink-tc32'
Get-Service -Name $serviceName
Get-CimInstance Win32_Service -Filter "Name='$serviceName'" | Select-Object Name, State, StartMode, StartName, PathName
~~~

应满足：

- State 为 Running；
- StartMode 为 Auto；
- StartName 为 NT AUTHORITY\NETWORK SERVICE；
- 注册过程已设置 delayed auto start 和 service recovery。

### 5.2 GitHub 在线状态

~~~powershell
gh api repos/CS19970929/telink-new-sdk-b85/actions/runners --jq '.runners[] | select(.name=="DESKTOP-UU5VC9R-telink-tc32") | {id,name,status,busy,labels:[.labels[].name]}'
~~~

应显示 status=online、busy=false，并包含四个要求标签。

### 5.3 服务模式完整复验

~~~powershell
gh workflow run bms-ci.yml --repo CS19970929/telink-new-sdk-b85 --ref refactor/bms-template-phase1
gh run list --repo CS19970929/telink-new-sdk-b85 --workflow bms-ci.yml --limit 5
gh run watch {RUN_ID} --repo CS19970929/telink-new-sdk-b85 --exit-status
~~~

只有 Host contract checks 和 TC32 production build 都成功，才能描述为完整 CI
通过。

## 6. 日常开发使用

### 6.1 提交前本地检查

修改头文件或生产源码后至少执行：

~~~powershell
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static
~~~

新增、删除或重命名源码时，先显式更新并审核 source_order.txt：

~~~powershell
python bms_tools/bms.py sources --update
git diff -- bms_tools/source_order.txt
python bms_tools/bms.py sources --check
~~~

### 6.2 自动触发

正常推送到 refactor/bms-template-phase1，或创建指向该分支的同仓库 PR。进入
GitHub Actions 页面检查两个 job 均为绿色。

外部 fork PR 的 TC32 job 被跳过是预期安全行为，不得为“让检查变绿”而绕过
事件来源门禁。

### 6.3 手动触发

~~~powershell
gh workflow run bms-ci.yml --repo CS19970929/telink-new-sdk-b85 --ref refactor/bms-template-phase1
~~~

手动触发适用于：

- 发布候选 commit 复验；
- Runner、Python、PowerShell 或 TC32 环境调整后复验；
- PC 重启后确认服务恢复；
- 下载已确认 commit 的新 Artifact。

## 7. Artifact 下载和使用

TC32 job 使用 actions/upload-artifact 上传：

- 825x_ble_sample.bin；
- 825x_ble_sample.elf；
- 825x_ble_sample.map；
- 825x_ble_sample.lst；
- build.log；
- fw_manifest.json；
- static 目录中的机器可读静态分析结果。

Artifact 名称为：

~~~text
telink-bms-{GIT_COMMIT_SHA}
~~~

默认保留 14 天。下载示例：

~~~powershell
gh run download {RUN_ID} --repo CS19970929/telink-new-sdk-b85 --name 'telink-bms-{GIT_COMMIT_SHA}' --dir C:/Users/Administrator/Downloads/telink-bms-artifact
~~~

独立检查 BIN：

~~~powershell
$bin = 'C:/Users/Administrator/Downloads/telink-bms-artifact/825x_ble_sample.bin'
Get-Item -LiteralPath $bin | Select-Object FullName, Length
Get-FileHash -LiteralPath $bin -Algorithm SHA256
~~~

还应打开 fw_manifest.json，确认：

- git.commit 等于计划发布的 commit；
- size_bytes 与下载 BIN 一致；
- sha256 与 Get-FileHash 一致；
- integrity.telink_postbuild.valid 为 true；
- target_configuration 风险已被理解；
- vendor library 和 source order 哈希存在。

可烧录文件只有后处理后的 825x_ble_sample.bin。825x_ble_sample.raw.bin 是中间
产物，禁止作为量产固件烧录。

## 8. Runner 日常维护

### 8.1 查看、启动和重启

~~~powershell
$serviceName = 'actions.runner.CS19970929-telink-new-sdk-b85.DESKTOP-UU5VC9R-telink-tc32'
Get-Service -Name $serviceName
Start-Service -Name $serviceName
Restart-Service -Name $serviceName
~~~

不要对 actions.runner.* 使用模糊批量停止命令；本机还有其他仓库的 Runner。

### 8.2 查看 Runner 日志

~~~powershell
Get-ChildItem C:/actions-runner/_diag -Filter 'Runner_*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 5 Name, Length, LastWriteTime
$latest = Get-ChildItem C:/actions-runner/_diag -Filter 'Runner_*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Get-Content -LiteralPath $latest.FullName -Tail 160
~~~

服务级错误也可以从 Application Event Log 查看：

~~~powershell
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='ActionsRunnerService'; StartTime=(Get-Date).AddHours(-1)} | Select-Object TimeCreated, LevelDisplayName, Message
~~~

日志和截图属于临时诊断材料，应放在用户临时目录，不能放进仓库。

### 8.3 PC 重启后检查

1. Get-Service 确认服务为 Running；
2. GitHub API 确认 Runner 为 online；
3. 手动触发一次 workflow_dispatch；
4. 确认 TC32 job 使用服务账户完成全部步骤；
5. 下载 Artifact 并核对 manifest 与 BIN SHA-256。

### 8.4 Runner 更新

当前未禁用 GitHub Runner 自动更新。升级期间不要强制终止正在执行的 job。
更新后执行：

~~~powershell
C:/actions-runner/bin/Runner.Listener.exe --version
~~~

然后按 5.3 节重新跑完整 CI。Runner 软件更新不授权升级 TC32 编译器、SDK、
预编译库、启动文件或链接脚本。

## 9. 暂停、下线和重新注册

### 9.1 暂停 TC32 调度

~~~powershell
gh variable set TELINK_TC32_CI_ENABLED --body 0 --repo CS19970929/telink-new-sdk-b85
~~~

这只暂停仓库 TC32 job，不会停止 Runner 服务。

### 9.2 让本机 Runner 离线

先确认 busy=false，再停止精确服务：

~~~powershell
$serviceName = 'actions.runner.CS19970929-telink-new-sdk-b85.DESKTOP-UU5VC9R-telink-tc32'
Stop-Service -Name $serviceName
~~~

长时间无人值守、怀疑凭据泄漏或需要维修机器时，建议同时把变量设为 0。

### 9.3 正常移除

1. 把 TELINK_TC32_CI_ENABLED 设为 0；
2. 确认 Runner busy=false；
3. 停止精确服务；
4. 在 GitHub Settings → Actions → Runners 中选择这个 Runner；
5. 使用 GitHub 页面生成的 fresh removal token 执行官方 remove 命令；
6. 确认 Windows 服务和 GitHub Runner 记录都已消失；
7. 再处理 C:/actions-runner 中的残留文件。

本地 --local 只应用于服务端记录已经删除、正常 remove 无法执行的恢复场景。
如果只删除本地配置但保留 GitHub 记录，页面会留下离线 Runner；如果只删除服务
但保留 .service 标记，下一次服务注册可能冲突。

不得删除或修改 C:/actions-runner-bms。

## 10. 常见故障

### 10.1 runner-registration 返回 404 Not Found

常见原因：

- URL 被复制成 Markdown 形式，包含方括号和圆括号；
- token 已过期、已使用或已暴露；
- token 后直接拼接了 --unattended，中间没有空格；
- token 属于其他 repository 或 organization。

处理：

1. 停止重试旧命令；
2. 刷新 GitHub Add new self-hosted runner 页面；
3. 点击页面 Copy 按钮，不要复制聊天中的富文本；
4. 检查 URL 和 token 参数边界；
5. 使用新 token 注册；
6. 注册后清空剪贴板。

### 10.2 A session for this runner already exists

表示相同 Runner 身份还有活动或未过期会话。

~~~powershell
Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -like 'C:\actions-runner\*' } | Select-Object ProcessId, ParentProcessId, Name, ExecutablePath, CommandLine
~~~

先确认没有前台 run.cmd 与 Windows 服务同时运行。停止精确实例并等待旧会话释放。
如果冲突持续存在，应先从 GitHub 删除这个 Runner 记录，再使用新 token 重新注册，
不能影响其他 Runner。

### 10.3 running scripts is disabled on this system

检查的是 Windows PowerShell 5.1，而不是 pwsh：

~~~powershell
C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -Command "Get-ExecutionPolicy -List"
~~~

当前基线要求 LocalMachine=RemoteSigned。修改后重启 Runner 服务并重新触发 CI。

### 10.4 python is not recognized

交互账户能运行 Python 不代表 NETWORK SERVICE 能运行。检查：

- C:/Tools/Python312/python.exe 是否存在；
- machine PATH 是否包含 C:/Tools/Python312；
- 共享目录 ACL 是否允许服务账户读取和执行；
- 修改 PATH 后是否重启了 Runner 服务。

不要通过给 Runner 配置 Administrator 密码规避 PATH 问题。

### 10.5 TC32 job 一直 queued

依次检查：

1. TELINK_TC32_CI_ENABLED 是否为 1；
2. Windows PC 是否开机并联网；
3. Runner 是否 online；
4. Runner 是否 busy；
5. 标签是否完整且大小写一致；
6. 是否有旧 job 占用 Runner；
7. workflow 的 branch 和 event 是否匹配。

### 10.6 找不到 libgcc.a、__muldi3 或 __divdi3

当前锁定的 Windows TC32 安装没有可用于该目标 ABI 的 libgcc.a。禁止链接 host、
ARM 或其他 TC32 版本的 libgcc。

本项目已把 DVC1124 CC2 换算改成经过范围证明的等价 32-bit 表达式，并通过测试
防止重新引入 64-bit runtime helper。遇到这类错误应检查新增代码的 64-bit 乘除，
不能从其他工具链复制库文件掩盖问题。

### 10.7 Artifact 缺失或内容不全

先看 TC32 job 具体失败步骤。Upload firmware and analysis evidence 使用 always，
但 if-no-files-found=ignore；如果构建在生成 BIN 前失败，Artifact 可以不存在或
只包含部分日志，不能视为生产固件。

## 11. Public repository 安全要求

- 外部 fork PR 只允许运行 GitHub-hosted contract checks；
- TC32 job 只允许运行 push、workflow_dispatch 和同仓库 PR；
- 不在 workflow、仓库、日志、Artifact 或聊天中保存注册 token；
- 不给 Runner 服务配置 Administrator 账户密码；
- 不把不可信下载文件或外部脚本放进 Runner 工作目录执行；
- 不使用 pull_request_target checkout 外部 PR 代码后调用 self-hosted runner；
- Runner 为持久主机，job 之间可能残留工作文件，不能按一次性虚拟机理解；
- 异常调度时先把 TELINK_TC32_CI_ENABLED 设为 0，再停止精确服务并审计；
- 任何安全修复都不能顺带修改固件协议、Flash 布局或硬件行为。

## 12. 当前验证证据

基线 commit：

~~~text
328883f07844a58575f144424d496b86d89b3080
~~~

前台 Runner 完整成功：

~~~text
GitHub Actions run 34699552972
~~~

最终 Windows 服务模式完整成功：

~~~text
GitHub Actions run 34700170802
Host contract checks: success
TC32 production build: success
~~~

最终 Artifact：

~~~text
name    : telink-bms-328883f07844a58575f144424d496b86d89b3080
BIN     : 825x_ble_sample.bin
size    : 99988 bytes
SHA-256 : 99cd627d145fb8b6af8d25d6481055b534f624a9a979f53ffbe98c82495d69cd
~~~

前台模式与服务模式 BIN 的 SHA-256 完全相同。

Actions 页面保留的两个中间失败属于部署排障证据：

- run 34699903403：NETWORK SERVICE 的 Windows PowerShell 执行策略阻止临时脚本；
- run 34700010764：服务账户 PATH 中找不到 Python。

两项均已修复，并由 run 34700170802 的完整成功复验覆盖。

## 13. 发布和硬件边界

CI 成功可以证明：

- 锁定源码顺序有效；
- host contract tests 通过；
- TC32 clean rebuild 成功；
- 官方后处理 BIN 可生成；
- MAP、size、manifest 和完整性校验通过；
- Cppcheck 指定范围完成；
- Artifact 可下载且 BIN 哈希与 manifest 一致。

CI 成功不能证明：

- DVC1124 I2C、CRC 和寄存器时序已经过实板确认；
- CHG/DSG 极性和 MOS 实际动作正确；
- 24S/20S 采样、电流方向、NTC、保护、均衡、断线检测已验证；
- Sleep/Wakeup、BLE、Modbus、Flash、OTA 已完成硬件回归；
- TLSR8251 与继承的 MCU_STARTUP_8258 内存边界一定正确。

烧录前仍需执行 docs/HARDWARE_VALIDATION.md 中的硬件矩阵。可烧录地址为
0x00000，禁止全片擦除，必须保护 0x74000..0x7FFFF。

## 14. 快速检查清单

每次发布候选版本：

- [ ] HEAD 是计划发布的 commit；
- [ ] 工作树没有意外修改；
- [ ] Host contract checks 成功；
- [ ] TC32 production build 成功；
- [ ] Runner 服务仍为 Running，GitHub 状态为 online；
- [ ] Artifact 名称包含正确 commit SHA；
- [ ] BIN、manifest 的大小和 SHA-256 一致；
- [ ] 烧录文件是 825x_ble_sample.bin，不是 raw.bin；
- [ ] 固件改动对应的实板验证已经记录；
- [ ] 已核对 TLSR8251/MCU_STARTUP_8258 风险；
- [ ] 未把 token、凭据或临时文件带入仓库。

## 15. 相关文件

- .github/workflows/bms-ci.yml：CI 可执行定义；
- docs/BUILD_AND_TEST.md：本地构建、测试和发布门禁；
- docs/ARCHITECTURE.md：BMS 分层和 AFE 适配边界；
- docs/HARDWARE_VALIDATION.md：实板验证矩阵和未决项；
- docs/DVC1124_HS_D008.md：HS-D008 与 DVC1124 硬件基线；
- bms_tools/bms.py：唯一构建和验证命令入口；
- bms_tools/source_order.txt：源码和对象链接顺序权威清单。
