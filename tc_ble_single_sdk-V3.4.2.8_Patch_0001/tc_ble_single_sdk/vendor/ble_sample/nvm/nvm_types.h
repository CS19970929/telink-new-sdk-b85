#pragma once
#include <stdint.h>
// #include <stddef.h>

typedef enum {
    NVM_OK = 0,
    NVM_ERR = -1,
    NVM_NOT_FOUND = -2,
    NVM_BAD_PARAM = -3,
    NVM_NO_SPACE = -4,
    NVM_CORRUPT = -5,
} nvm_status_t;

typedef struct {
    uint32_t base;
    uint32_t sectors;
    uint32_t sector_size;
} nvm_region_t;
