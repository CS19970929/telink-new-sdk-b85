# D008 参数更新与出厂操作

普通 BLE 或串口 OTA 使用现有 `0x00000/0x20000` 槽和 SDK 启动标记。D008 源码默认 `update_groups=[]`，保留已提交的 Config、State、Event。升级取得 Flash 独占前，应用利用时间空窗尝试保存 State 和一条待写历史，错过空窗时继续升级。传输、CRC、槽激活与两种 OTA 的并发互斥沿用原入口。

使用 `python bms_tools/build_options.py show` 查看源码选择。显式选择组时通过 `save --update-groups sw afe business soc soc_state` 指定实际需要的子集。一次性 ID 为该源码参数包生成，按域与数据一起提交。同一个包重复启动不重复应用。传入空的 `--update-groups` 恢复普通保参策略。

`factory_reset` 是独立选项。新默认 Config 和进行中标记首先提交，随后清理并验证活跃 Config 的旧记录、State 和 Event，再提交完成标记。中断后用同一标记续做，已经核对的空域跳过擦除。全部完成后才能恢复输出。其写入范围见 [Flash 存储](STORAGE.md)。

固件装配与持久 Config 的串数、类型不一致时需工装准备新装配，或依生产合同用明确的出厂包。旧 CFG4、ST4/ST5 和 Event 快照属于历史工程，量产分支无旧数据迁移。构建和发布可审阅 [D008 构建与测试](BUILD_AND_TEST.md)。
