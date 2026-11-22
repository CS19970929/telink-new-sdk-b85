#ifndef CT_CONFIG_H
#define CT_CONFIG_H

#include <stdint.h>

#define CT_PROTOCOL_VERSION            1u
#define CT_FW_VERSION_MAJOR            0u
#define CT_FW_VERSION_MINOR            2u
#define CT_FW_VERSION_PATCH            10u

#define CT_BUILD_PROFILE_RELEASE       0u
#define CT_BUILD_PROFILE_DEBUG         1u

#ifndef CT_BUILD_PROFILE
#define CT_BUILD_PROFILE               CT_BUILD_PROFILE_RELEASE
#endif

#ifndef CT_DEBUG_LOG_ENABLE
#if (CT_BUILD_PROFILE == CT_BUILD_PROFILE_DEBUG)
#define CT_DEBUG_LOG_ENABLE            1u
#else
#define CT_DEBUG_LOG_ENABLE            0u
#endif
#endif

#define CT_DEBUG_LOG_CAPACITY          64u

#define CT_UART_MAX_PAYLOAD            512u
#define CT_UART_DEFAULT_BAUD           115200u
#define CT_BMS_UART_BAUD               19200u
#define CT_COMM_UART_PORT_USART1       1u
#define CT_COMM_UART_PORT_USART3       3u

#ifndef CT_COMM_UART_PORT
#define CT_COMM_UART_PORT              CT_COMM_UART_PORT_USART1
#endif

#define CT_CAN_DEFAULT_BITRATE         250000u
#define CT_NODE_ID_DEFAULT             1u

#ifndef CT_WATCHDOG_ENABLE
#define CT_WATCHDOG_ENABLE             1u
#endif

#ifndef CT_WATCHDOG_RELOAD_VALUE
#define CT_WATCHDOG_RELOAD_VALUE       1875u
#endif

#define CT_C073_APP_BASE               0x08003000u
#define CT_C073_APP_LIMIT              0x0800E800u
#define CT_C073_COMPACT_CAPACITY       0xB000u
#define CT_C073_ORIGINAL_CAPACITY      (CT_C073_APP_LIMIT - CT_C073_APP_BASE)
#define CT_C073_BOARD                  0xC073000Du

#define CT_BMS_IAP_BASE_ADDR           0x08000000u
#define CT_BMS_APP_BASE_ADDR           0x08004800u
#define CT_BMS_APP_LIMIT_ADDR          0x0801F800u

#define CT_SELF_FLASH_BASE             0x08000000u
#define CT_SELF_FLASH_SIZE             0x00080000u
#define CT_SELF_FLASH_END              (CT_SELF_FLASH_BASE + CT_SELF_FLASH_SIZE)
#define CT_SELF_FLASH_PAGE_SIZE        0x800u
#define CT_SELF_IAP_BASE               0x08000000u
#define CT_SELF_IAP_SIZE               0x00008000u
#define CT_SELF_APP_BASE               0x08008000u
#define CT_SELF_APP_SIZE               0x00010000u
#define CT_SELF_APP_LIMIT              (CT_SELF_APP_BASE + CT_SELF_APP_SIZE)
#define CT_SELF_CAN_APP_ADDR           0x0Eu
#define CT_BOOT_MAILBOX_ADDR           0x2000FFE0u
#define CT_BOOT_MAILBOX_MAGIC          0x43544950u
#define CT_BOOT_MAILBOX_REQUEST        0x0000A501u

#define CT_FW_CACHE_BASE               0x08018000u
#define CT_FW_META_PAGE                0x0807F800u
#define CT_FW_CACHE_LIMIT              CT_FW_META_PAGE
#define CT_FW_CACHE_SIZE               (CT_FW_CACHE_LIMIT - CT_FW_CACHE_BASE)

#define CT_IAP_BLOCK_FRAMES            32u
#define CT_IAP_BLOCK_BYTES             (CT_IAP_BLOCK_FRAMES * 8u)

#endif
