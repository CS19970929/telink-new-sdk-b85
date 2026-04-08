# BMS 客户端资产工具使用说明

## 1. 文档目的

本文说明当前仓库里新增的跨平台客户端资产工具如何使用。  
这些工具的目标不是替代具体平台开发，而是提供两类长期可复用资产：

- 统一寄存器真源
- 统一协议测试向量

并在此基础上提供：

- 多语言常量文件生成
- 当前 Python 协议实现回归校验

---

## 2. 输入资产

当前工具依赖以下两个输入文件：

- `docs/register_catalog.json`
- `docs/protocol_test_vectors.json`

它们的定位分别是：

- `register_catalog.json`
  - 面向“定义”
  - 负责保存寄存器目录、字段索引、状态位、BLE 约束、统一动作映射
- `protocol_test_vectors.json`
  - 面向“验证”
  - 负责保存请求帧、响应帧、分片样例、快照解码样例

建议以后所有平台开发都优先修改这两个输入，而不是在各端代码里各自维护一套副本。

---

## 3. 工具脚本位置

脚本位置：

- `script/bms_client_asset_tool.py`

这是一个统一入口，当前支持 3 个命令：

- `generate`
- `verify`
- `all`

---

## 4. generate 命令

### 4.1 作用

从 `docs/register_catalog.json` 生成多语言寄存器常量文件。

### 4.2 用法

在 `tc_ble_single_sdk` 根目录执行：

```bash
python3 script/bms_client_asset_tool.py generate
```

### 4.3 当前输出文件

生成目录：

- `docs/generated/`

当前输出：

- `docs/generated/BMSGeneratedRegisterCatalog.swift`
- `docs/generated/bms_generated_register_catalog.py`
- `docs/generated/BmsGeneratedRegisterCatalog.kt`
- `docs/generated/bms_generated_register_catalog.hpp`
- `docs/generated/README.md`

### 4.4 使用建议

- iPhone / iPad / mac 原生端：可直接参考 `Swift` 生成文件
- Python / Qt 工具：可直接参考 `Python` 生成文件
- Android：可直接参考 `Kotlin` 生成文件
- Windows 原生或 C++ 工具：可直接参考 `C++ header` 生成文件

注意：

- 这些文件是“生成产物”，不建议手工修改
- 如需更新，应修改 `register_catalog.json` 后重新执行 `generate`

---

## 5. verify 命令

### 5.1 作用

使用当前 Qt/Python 实现，对共享协议资产做一轮自动校验，覆盖：

- 请求帧编码
- 响应帧解析
- CRC 错误识别
- notify 分片重组
- `BatteryStatusSnapshot` 快照解码

### 5.2 用法

```bash
python3 script/bms_client_asset_tool.py verify
```

### 5.3 校验逻辑来源

当前 `verify` 会直接复用：

- `vendor/ble_sample/BMSAssistantQt/bmsassistantqt/protocol.py`
- `vendor/ble_sample/BMSAssistantQt/bmsassistantqt/models.py`

也就是说，它不是只检查 JSON 格式，而是拿现有客户端协议实现做回归验证。

### 5.4 适用场景

适合在以下场景执行：

- 你修改了寄存器目录
- 你增加了协议测试向量
- 你调整了电池状态解码规则
- 你准备开发新客户端前，先确认共享资产没有偏移

---

## 6. all 命令

### 6.1 作用

先生成，再校验。

### 6.2 用法

```bash
python3 script/bms_client_asset_tool.py all
```

这适合做成日常开发前的标准动作，或者以后接到 CI 里。

---

## 7. 推荐工作流

建议后续统一采用下面的流程。

### 7.1 修改协议或寄存器时

1. 先改 `docs/register_catalog.json`
2. 如有必要，再改 `docs/protocol_test_vectors.json`
3. 执行：

```bash
python3 script/bms_client_asset_tool.py all
```

4. 确认生成产物和校验结果无误
5. 再去修改具体平台客户端代码

### 7.2 新做一个客户端时

建议顺序：

1. 先读：
   - `docs/mac与Qt上位机BLE共性架构梳理.md`
   - `docs/BMS客户端跨平台统一架构与开发规范.md`
2. 再直接使用：
   - `docs/generated/*.swift / *.kt / *.hpp / *.py`
3. 最后用：
   - `docs/protocol_test_vectors.json`
   - `script/bms_client_asset_tool.py verify`
   做协议回归

---

## 8. 当前工具的边界

当前工具已经能做：

- 生成多语言寄存器常量
- 跑请求编码校验
- 跑响应解析校验
- 跑分片重组校验
- 跑电池快照解码校验

当前还没有做的内容：

- 自动生成完整 ViewModel
- 自动生成完整业务 Service
- 自动生成 UI 页面
- 多语言单元测试工程模板

所以它当前定位是“共享资产生成器 + 协议回归工具”，不是完整客户端脚手架。

---

## 9. 后续建议

如果后面你继续往“多平台统一资产”推进，下一步最值得补的是：

1. 从 `protocol_test_vectors.json` 自动生成各语言单元测试模板
2. 从 `register_catalog.json` 自动生成平台无关字段文档
3. 增加一个 `golden_snapshot.json`，专门保存真实板子抓包后的黄金样例

这样以后每增加一个客户端，实现成本会进一步下降。
