#ifndef BMS_SELFTEST_CFG_H_
#define BMS_SELFTEST_CFG_H_

/* Production defaults: self-test enabled, destructive fault injection disabled. */
#ifndef BMS_SELFTEST_ENABLE
#define BMS_SELFTEST_ENABLE                       1
#endif

#ifndef BMS_SELFTEST_RUNTIME_PERIOD_US
#define BMS_SELFTEST_RUNTIME_PERIOD_US            100000u
#endif

#ifndef BMS_SELFTEST_FLASH_BLOCK_BYTES
#define BMS_SELFTEST_FLASH_BLOCK_BYTES            64u
#endif

#ifndef BMS_SELFTEST_RAM_WORDS
#define BMS_SELFTEST_RAM_WORDS                    64u
#endif

#ifndef BMS_SELFTEST_RAM_RUNTIME_WORDS
#define BMS_SELFTEST_RAM_RUNTIME_WORDS            4u
#endif

#ifndef BMS_SELFTEST_IRQ_PERIOD_US
#define BMS_SELFTEST_IRQ_PERIOD_US                500u
#endif

#ifndef BMS_SELFTEST_IRQ_MIN_PER_WINDOW
#define BMS_SELFTEST_IRQ_MIN_PER_WINDOW           20u
#endif

#ifndef BMS_SELFTEST_IRQ_MAX_PER_WINDOW
#define BMS_SELFTEST_IRQ_MAX_PER_WINDOW           1000u
#endif

#ifndef BMS_SELFTEST_HEARTBEAT_WINDOW_US
#define BMS_SELFTEST_HEARTBEAT_WINDOW_US           1500000u
#endif

#ifndef BMS_SELFTEST_ADC_FAULT_LIMIT
#define BMS_SELFTEST_ADC_FAULT_LIMIT              3u
#endif

#ifndef BMS_SELFTEST_AFE_FAULT_LIMIT
#define BMS_SELFTEST_AFE_FAULT_LIMIT              3u
#endif

#ifndef BMS_SELFTEST_MOS_FAULT_LIMIT
#define BMS_SELFTEST_MOS_FAULT_LIMIT              2u
#endif

#ifndef BMS_SELFTEST_STACK_GUARD_WORDS
#define BMS_SELFTEST_STACK_GUARD_WORDS            8u
#endif

#ifndef BMS_SELFTEST_STACK_FILL_MARGIN
#define BMS_SELFTEST_STACK_FILL_MARGIN            96u
#endif

/* Certification-only build switches. Remote fault injection is never compiled. */
#ifndef BMS_DIAG_TEST_BUILD
#define BMS_DIAG_TEST_BUILD                       0
#endif

#ifndef BMS_DIAG_FAULT_INJECT_ENABLE
#define BMS_DIAG_FAULT_INJECT_ENABLE              0
#endif

#ifndef BMS_FAULT_INJECT_MASK
#define BMS_FAULT_INJECT_MASK                     0u
#endif

#if BMS_DIAG_FAULT_INJECT_ENABLE && !BMS_DIAG_TEST_BUILD
#error "Fault injection requires an explicitly marked BMS_DIAG_TEST_BUILD"
#endif

#if !BMS_DIAG_FAULT_INJECT_ENABLE && (BMS_FAULT_INJECT_MASK != 0u)
#error "BMS_FAULT_INJECT_MASK must be zero in production builds"
#endif

#endif
