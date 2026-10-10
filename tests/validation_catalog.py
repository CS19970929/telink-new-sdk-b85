"""测试适用产品、证据层级和已知边界；新增检查必须显式登记，防止漏跑。"""
PRODUCTS = ('d008', 'd011', 'd013', 'd014')
SH = PRODUCTS[1:]
CHECKS = {}


def register(domain, products, evidence, names):
    for name in names.split():
        if name in CHECKS:
            raise ValueError('重复测试登记：'+name)
        CHECKS[name] = {'domain': domain, 'products': products, 'evidence': evidence}


register('afe', ('d014',), '22个原始生产TU与配置回滚故障', 'afe_configuration_gate_host_check')
register('afe', PRODUCTS, '原厂范围独立向量与真实校验函数体', 'afe_factory_regression_host_check')
register('afe', PRODUCTS, '提取函数故障注入', 'afe_hw_transaction_host_check')
register('power', ('d008',), '提取函数场景', 'app_scheduler_host_check')
register('storage', ('d014',), '提取函数场景', 'bms_simplification_host_check')
register('protection', PRODUCTS, '源码 contract', 'core_contract_check')
register('soc', PRODUCTS, '生产 K/B 与独立数学 oracle', 'current_calibration_host_check')
register('afe', ('d008',), '源码 contract', 'd008_backend_contract_check')
register('configuration', ('d008',), '产品编译分支/源码 contract', 'd008_configuration_check')
register('soc', ('d008',), '生产 SOC/部分 PM 与环境替身', 'd008_power_soc_host_check')
register('afe', ('d008',), '提取函数故障注入', 'd008_recovery_host_check')
register('configuration', ('d014',), '源码 contract；板级源码 contract', 'd014_configuration_contract_check')
register('protection', ('d014',), '提取函数场景', 'd014_defaults_host_check')
register('protection', ('d014',), '22 个原始生产 TU 与 SPI/Flash 端口模型', 'd014_safety_loop_host_check')
register('diagnostics', PRODUCTS, '生产代码与环境替身', 'diagnostics_host_check')
register('afe', ('d008',), '提取函数故障注入', 'dvc1124_fixed_policy_host_check')
register('afe', ('d008',), '实际DVC读取/采样函数体与原厂RC事件向量', 'dvc_adc_events_host_check')
register('afe', ('d008',), '真实温度采集/有效性/仲裁函数体与完整公共保护；RAM 寄存器非物理 Gate', 'dvc_ntc_scenarios_host_check')
register('afe', SH, '真实检测函数与读清寄存器 peer；不替代物理映射及 Gate', 'openwire_backends_host_check')
register('afe', ('d008',), '提取函数故障注入', 'dvc_register_scenarios_host_check')
register('protection', PRODUCTS, '完整 feature 与真实板级能力', 'features_scenarios_host_check')
register('storage', PRODUCTS, '生产 journal/语义存储与 RAM Flash', 'flash_quick_check')
register('protocol', PRODUCTS, '完整 Modbus 模块与真实 CRC/State/diag/logger，寄存器所有者替身；真实 parser/CRC 与寄存器替身', 'modbus_host_check')
register('tooling', ('d014',), '完整可移植核心 TU', 'portable_core_host_check')
register('configuration', PRODUCTS, '真实默认构造与校验', 'product_configuration_host_check')
register('tooling', PRODUCTS, '实际编译预处理', 'production_policy_check')
register('protection', PRODUCTS, '生产 TU 场景', 'protection_scenarios_host_check')
register('protocol', ('d011', 'd014'), '固定 RS485 源码 contract', 'sh3673510_comm_mode_check')
register('configuration', ('d011',), '板级源码 contract', 'sh3673510_d011_integration_check')
register('soc', SH, '提取函数场景', 'sh3673510_soc_direction_host_check')
register('afe', ('d014',), '公共芯片驱动/量化场景', 'sh3673520_contract_check')
register('afe', SH, '源码 contract', 'sh_backend_contract_check')
register('power', SH, '生产函数体与环境替身', 'sh_power_host_check')
register('afe', ('d014',), '公共芯片驱动/量化场景', 'sh_quantize_host_check')
register('afe', SH, '生产函数体与环境替身', 'sh_recovery_host_check')
register('afe', SH, '完整 control 函数体与总线模型；原厂SCT四位枚举与完整control函数体；实际SH测量发布函数体与明确协议范围；生产函数体与环境替身', 'sh_register_scenarios_host_check')
register('protection', SH, '真实 backend/guard/SW 联合场景', 'sh_safety_chain_host_check')
register('afe', SH, '生产函数体与环境替身', 'sh_sample_atomic_host_check')
register('storage', SH, '提取函数故障注入', 'sh_storage_host_check')
register('protocol', ('d008',), '真实封包与固定 wire 向量', 'sif_packet_host_check')
register('tooling', ('d014',), '缓存回归', 'soc_replay_freshness_check')
register('soc', PRODUCTS, '生产 SOC 函数体与环境替身', 'soc_scenarios_host_check')
register('storage', PRODUCTS, '生产 journal/语义存储与 RAM Flash', 'storage_host_check')
register('protection', PRODUCTS, '生产 TU 场景', 'sw_temperature_groups_host_check')
register('tooling', ('d014',), '工具行为/源码 contract', 'tooling_contract_check')
register('protocol', PRODUCTS, 'UART DMA/IRQ 软件模型', 'uart_ownership_host_check')
register('tooling', ('d014',), '故意变异检验', 'validation_mutation_check')

SOURCES = {
    'protection': ['bms/core/bms_sw_protection.c', 'bms/app/bms_features.c'],
    'configuration': ['bms/core/bms_parameters.h', 'bms/core/bms_afe_hw_profile.c', 'bms/products'],
    'afe': ['bms/core/bms_afe_guard.c', 'bms/core/bms_afe_hw_profile.c', 'bms/afe'],
    'soc': ['bms/core/bms_soc.c', 'bms/core/bms_soc_eta.c', 'bms/app/app.c'],
    'storage': ['bms/core/storage_record.c', 'bms/core/bms_config_store.c', 'bms/core/bms_state_store.c', 'bms/core/bms_event_log.c'],
    'protocol': ['bms/core/modbus_rtu.c', 'bms/core/bms_parameter_access.c', 'bms/platform/telink/modbus_uart.c'],
    'diagnostics': ['bms/core/bms_diag.c', 'bms/core/bms_debug_log.c'],
    'power': ['bms/app/app.c', 'bms/app/app_power.c', 'bms/core/bms_afe_guard.c'],
    'tooling': ['bms_tools', 'tests'],
}

BLIND_SPOTS = [
    {'risk':'高','area':'完整系统 SIL','gap':'D014 已连接真实参数/journal/SH SPI/feature/guard/SW 到 FET 命令；完整 MCU 调度、DVC 联合链、物理 Gate 和跨 MCU reset 的软件 OC 锁存策略仍未闭合'},
    {'risk':'高','area':'AFE 物理行为','gap':'RAM 寄存器不模拟硅片比较器、ADC 误差、转换时延或 watchdog 断总线后的 Gate；官方手册/板级读回仍需独立核验'},
    {'risk':'高','area':'产品参数','gap':'D013 缺受控原理图；容量/阈值未全部签核；D008 SCD 与各化学体系编译默认不代替实板保护和 MOS SOA 验收'},
    {'risk':'高','area':'SOC/电流','gap':'长时算法场景使用受控 100Ah 环境；开机零点的真实零电流、SH 新鲜温度标志、C+ 判据和整板标定仍需台架'},
    {'risk':'高','area':'存储/升级','gap':'逐字节故障模型不等于 Flash 擦除掉电、电源瞬态或实际 OTA；三域分别提交不构成跨域原子事务'},
    {'risk':'中','area':'协议','gap':'parser 随机测试的寄存器所有者为替身；BLE SDK/分片/RF、UART 任意粘包吞吐、PHY 仍需硬件；CAN 未进入当前四产品真实源码清单'},
    {'risk':'中','area':'调度/并发','gap':'host 不证明中断最坏时延、栈水位、TC32 ABI/栈溢出、实际 Sleep 电流；需 ELF/MAP 和实测'},
    {'risk':'中','area':'覆盖指标','gap':'不把测试组数或断言数当行/分支覆盖率；未测量的路径显式保留未知，不给虚构覆盖百分比'},
]
