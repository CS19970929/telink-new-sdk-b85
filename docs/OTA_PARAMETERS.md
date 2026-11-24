# D008 参数更新与出厂操作

普通 BLE 或串口 OTA 使用现有 `0x00000/0x20000` 槽和 SDK 启动标记。D008 源码默认 `update_groups=[]`，保留已提交的 Config、State、Event。升级取得 Flash 独占前，应用利用时间空窗尝试保存 State 和一条待写历史，错过空窗时继续升级。传输、CRC、槽激活与两种 OTA 的并发互斥沿用原入口。

使用 `python bms_tools/build_options.py show` 查看源码选择。显式选择组时通过 `save --update-groups sw afe business soc soc_state` 指定实际需要的子集。一次性 ID 为该源码参数包生成，按域与数据一起提交。同一个包重复启动不重复应用。传入空的 `--update-groups` 恢复普通保参策略。

`factory_reset` 是独立选项，源码宏为 `BMS_OTA_UPDATE_FACTORY_RESET`。可以在配置窗口勾选恢复出厂并生成新的参数包标识，或使用以下命令保存到源码。

```text
python -B bms_tools/build_options.py save --update-groups factory_reset --new-package
```

新默认 Config 和进行中标记首先提交。随后清理活跃 Config 的旧记录、State、Event 和旧版余下的业务扇区，最后提交完成标记。中断后在下次启动用同一标记续做，复核已擦空的扇区并跳过。全部完成后才恢复输出资格。其写入范围见 [Flash 存储](STORAGE.md)。

## 从 D008 四产品基线切换

旧板的串数、化学体系、额定容量和板级配置须与新镜像相符。对于 `2e02f32e9af2` 的 512 KiB、`0x20000` 双槽 D008，可使用一次性出厂包后再使用普通保参镜像。若由工装预处理，按 [Flash 存储](STORAGE.md) 的旧版业务区边界定向擦除。出厂初始化会清除 BMS 存储的 SN、BLE 名称覆盖值、电流校准、客户保护参数、SOC、循环和历史。MCU 的 BLE MAC、SDK 配对和校准区在此操作范围外。

固件装配与持久 Config 的串数、类型不一致时需工装准备新装配，或依生产合同用明确的出厂包。旧 CFG4、ST4/ST5 和 Event 快照属于历史工程，量产分支无旧数据迁移。构建和发布可审阅 [D008 构建与测试](BUILD_AND_TEST.md)。
