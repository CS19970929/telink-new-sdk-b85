# 局域网剪贴板同步小工具

## 目标

这个工具用于在同一局域网中的多台设备之间同步文本剪贴板，优先支持：

- macOS
- Windows
- Linux（可选）

当前版本解决的问题是：

- Windows 上复制文本，Mac 上直接粘贴
- Mac 上复制文本，Windows 上直接粘贴
- 不依赖第三方云服务
- 不依赖额外 Python 包，使用标准库即可运行
- 预留后续扩展空间，便于增加适合你的定制功能

## 方案特点

- **零第三方依赖**：直接用 `python3` 运行
- **本地网络同步**：每台机器本地监听 HTTP，同时轮询本机剪贴板
- **双向同步**：任一端复制，都会推送到配置的对端
- **基础安全**：使用共享 `token` 做简单鉴权
- **防回环**：收到远端内容后不会立刻反向再发一遍
- **独立目录**：不会影响当前仓库里的 BLE / BMS 代码

## 当前目录结构

```text
tools/lan_clipboard_sync/
├── README.md
├── config.example.json
├── run.py
├── start_mac.command
├── start_win.bat
├── lanclip/
│   ├── __init__.py
│   ├── clipboard.py
│   ├── cli.py
│   ├── config.py
│   ├── core.py
│   └── http_server.py
└── tests/
    └── test_core.py
```

## 架构说明

### 模块边界

- `config.py`
  负责配置文件加载、保存、默认值管理
- `clipboard.py`
  负责不同操作系统的剪贴板读写适配
- `core.py`
  负责核心同步逻辑、消息校验、去重、防回环
- `http_server.py`
  负责接收其他设备发来的同步请求
- `cli.py`
  负责初始化配置和启动服务

### 核心链路

1. 本地进程轮询本机剪贴板
2. 如果检测到内容变化，则构造 `ClipMessage`
3. 通过 HTTP `POST /clip` 推送到已配置对端
4. 对端校验 `token` 和消息内容
5. 对端写入本机剪贴板
6. 对端使用内容哈希做短时间抑制，避免反向回环

## 快速开始

### 1. Mac 上生成配置

进入工具目录：

```bash
cd tools/lan_clipboard_sync
```

生成配置：

```bash
python3 run.py init \
  --output config.json \
  --node-id mac-dev \
  --peer win-dev=http://192.168.1.20:52821/clip
```

执行后会输出一个 `Shared token`。

### 2. Windows 上生成配置

在 Windows 上把同一个工具目录拷过去，或者放到你自己的代码目录中。

执行：

```powershell
py -3 run.py init `
  --output config.json `
  --node-id win-dev `
  --token <上一步生成的 Shared token> `
  --peer mac-dev=http://192.168.1.10:52821/clip
```

注意把：

- `192.168.1.10` 改成 Mac 的局域网 IP
- `192.168.1.20` 改成 Windows 的局域网 IP

如果你希望两边使用同一个固定 token，也可以直接手工编辑 `config.json`。

### 3. 启动

Mac：

```bash
python3 run.py run --config config.json
```

Windows：

```powershell
py -3 run.py run --config config.json
```

### 4. 一键启动

- Mac 可双击 `start_mac.command`
- Windows 可双击 `start_win.bat`

首次运行时，macOS 可能要求你授权终端或 Python 的辅助功能权限。

## 配置说明

示例：

```json
{
  "node_id": "mac-dev",
  "shared_token": "replace-with-your-shared-token",
  "listen_host": "0.0.0.0",
  "listen_port": 52821,
  "poll_interval_ms": 500,
  "clipboard_max_chars": 200000,
  "allowed_sender_ips": [],
  "peers": [
    {
      "name": "win-dev",
      "url": "http://192.168.1.20:52821/clip",
      "enabled": true,
      "timeout_seconds": 2.5
    }
  ]
}
```

关键字段：

- `node_id`
  当前设备标识，建议固定且可读
- `shared_token`
  所有设备保持一致，用于简单鉴权
- `listen_port`
  本地监听端口，默认 `52821`
- `poll_interval_ms`
  剪贴板轮询周期，默认 `500ms`
- `clipboard_max_chars`
  单次允许同步的最大文本长度
- `allowed_sender_ips`
  可选的发送端 IP 白名单
- `peers`
  需要同步到的其他设备列表

## 验证方法

### 连通性验证

在另一台机器上访问：

```bash
curl http://<目标IP>:52821/healthz
```

期望返回：

```json
{"status": "ok"}
```

### 功能验证

1. 在 Windows 上复制一段文本
2. 等待约 `0.5s ~ 1s`
3. 在 Mac 上粘贴
4. 再反向测试一次

## 已知限制

- 当前只支持**文本剪贴板**
- 不支持图片、文件、富文本
- 多设备场景下建议先采用“每台都配置所有其他设备”的方式
- 当前使用轮询，不是系统级事件监听

## 后续扩展建议

这个版本已经预留了比较清晰的边界，后续适合继续加这些能力：

- **设备白名单和命名管理**
  让新设备接入更可控
- **内容过滤器**
  例如忽略密码、OTP、过长日志
- **快捷短语 / 代码片段中心**
  把常用命令、寄存器配置、测试模板同步到各设备
- **历史剪贴板**
  支持最近 N 条快速回放
- **文件与图片同步**
  为后续传文件和截图做扩展
- **消息加密**
  在共享办公网络中更稳妥
- **托盘应用**
  降低命令行使用成本
- **自动发现**
  通过 mDNS / Bonjour 自动发现局域网设备

## 适合你的下一步

如果你后续要“更适合自己”，我建议按下面顺序演进：

1. 先把这个版本跑通
2. 再加一个“忽略敏感关键字”的过滤器
3. 再加“历史剪贴板 + 快捷短语”
4. 最后再做 GUI / 菜单栏常驻

这样每一步都能独立验证，不会一次性把复杂度做炸。
