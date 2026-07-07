#ifndef SAFETY_CONFIG_H_
#define SAFETY_CONFIG_H_

/* 安全框架总开关。默认打开框架，但高风险破坏性测试仍由独立开关控制。 */
#ifndef SAFETY_ENABLE
#define SAFETY_ENABLE                         1
#endif

/* 启动阶段执行不破坏业务状态的自检。 */
#ifndef SAFETY_STARTUP_TEST_ENABLE
#define SAFETY_STARTUP_TEST_ENABLE            1
#endif

/* 主循环中以非阻塞方式执行周期自检。 */
#ifndef SAFETY_RUNTIME_TEST_ENABLE
#define SAFETY_RUNTIME_TEST_ENABLE            1
#endif

/* 运行期自检周期，单位 us。 */
#ifndef SAFETY_RUNTIME_PERIOD_US
#define SAFETY_RUNTIME_PERIOD_US              100000u
#endif

/* 当前固件尚未量产写入 CRC 元数据，默认不强制 Flash CRC。 */
#ifndef SAFETY_FLASH_CRC_ENFORCE
#define SAFETY_FLASH_CRC_ENFORCE              0
#endif

/* WDT 上电复位自测会主动等待复位，默认关闭，认证测试时再打开。 */
#ifndef SAFETY_WATCHDOG_STARTUP_RESET_TEST_ENABLE
#define SAFETY_WATCHDOG_STARTUP_RESET_TEST_ENABLE 0
#endif

/* AFE 通信连续异常次数达到阈值后进入 Fail Safe。 */
#ifndef SAFETY_AFE_COMM_FAULT_LIMIT
#define SAFETY_AFE_COMM_FAULT_LIMIT           3u
#endif

/* ADC 合理性检查采用宽范围，只拦截明显无效数据，避免改变现有保护阈值。 */
#ifndef SAFETY_CELL_VOLT_MIN_MV
#define SAFETY_CELL_VOLT_MIN_MV               500u
#endif

#ifndef SAFETY_CELL_VOLT_MAX_MV
#define SAFETY_CELL_VOLT_MAX_MV               5000u
#endif

#ifndef SAFETY_TEMP_RAW_MAX
#define SAFETY_TEMP_RAW_MAX                   2000u
#endif

#ifndef SAFETY_CURRENT_MAX_A10
#define SAFETY_CURRENT_MAX_A10                30000u
#endif

/* RAM 自检使用框架自有缓冲区，避免破坏栈和业务变量。 */
#ifndef SAFETY_RAM_TEST_WORDS
#define SAFETY_RAM_TEST_WORDS                 32u
#endif

#ifndef SAFETY_RAM_RUNTIME_WORDS
#define SAFETY_RAM_RUNTIME_WORDS              4u
#endif

/* Flash CRC 元数据格式预留，量产工具写入后可打开强制校验。 */
#ifndef SAFETY_FLASH_CRC_INFO_ADDR
#define SAFETY_FLASH_CRC_INFO_ADDR            0x7F000u
#endif

#ifndef SAFETY_FLASH_CRC_MAGIC
#define SAFETY_FLASH_CRC_MAGIC                0x53464352u
#endif

#ifndef SAFETY_FLASH_CRC_VERSION
#define SAFETY_FLASH_CRC_VERSION              1u
#endif

#ifndef SAFETY_FLASH_PARTIAL_BYTES
#define SAFETY_FLASH_PARTIAL_BYTES            256u
#endif

/* 故障注入只在认证或台架测试时打开。 */
#ifndef SAFETY_TEST_ENABLE
#define SAFETY_TEST_ENABLE                    0
#endif

#ifndef SAFETY_INJECT_CPU_FAULT
#define SAFETY_INJECT_CPU_FAULT               0
#endif

#ifndef SAFETY_INJECT_FLASH_FAULT
#define SAFETY_INJECT_FLASH_FAULT             0
#endif

#ifndef SAFETY_INJECT_RAM_FAULT
#define SAFETY_INJECT_RAM_FAULT               0
#endif

#ifndef SAFETY_INJECT_CLOCK_FAULT
#define SAFETY_INJECT_CLOCK_FAULT             0
#endif

#ifndef SAFETY_INJECT_WDT_FAULT
#define SAFETY_INJECT_WDT_FAULT               0
#endif

#ifndef SAFETY_INJECT_ADC_FAULT
#define SAFETY_INJECT_ADC_FAULT               0
#endif

#ifndef SAFETY_INJECT_AFE_COMM_FAULT
#define SAFETY_INJECT_AFE_COMM_FAULT          0
#endif

#endif
