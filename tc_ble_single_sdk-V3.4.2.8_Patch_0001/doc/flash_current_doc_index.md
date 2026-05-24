# Flash 文档索引

## 0. ble_sample 全量梳理

如果要看当前 `ble_sample` 的启动、SOC、KV、老化工厂模式、低功耗和通信全链路，请优先看：

- `ble_sample_full_logic_review_20260524.md`
- `ble_sample_full_logic_review_20260524.html`
- `soc_user_experience_tuning_20260524.md`

其中 `soc_user_experience_tuning_20260524.md` 专门整理当前 SOC 修改点、可调参数、增减影响、验证场景，以及这些参数对 BLE/Modbus/SIF、KV、deep sleep、老化 runtime 的影响边界。

## 1. 当前有效 Flash 文档

如果要看当前版本，请优先看下面几份：

- `flash_full_review_and_optimization_20260408.md`
- `project_flash_map_8251_512k.md`
- `flash_lifetime_and_simplification_assessment_8251_512k.md`
- `soc_kv_immediate_write_lifetime_assessment_20260408.md`
- `bms_cold_kv_store_usage.md`
- `flash_btname_merge_and_no_migration_changes.md`

## 2. 历史文档

下面这些更多是阶段性整改记录或设计演进记录，不再代表当前最终状态：

- `ble_sample_flash整改说明.md`
- `flash_review_risk_assessment.md`
- `flash_storage_optimization_changes.md`
- `soc_kv_store_analysis_and_refactor.md`

阅读这些历史文档时，需要注意两点：

- 当前版本已经删除旧布局迁移逻辑
- `btname` 已经并入 `cold_kv`
