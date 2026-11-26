#pragma once
#include <stdint.h>
#include <stdbool.h>
extern uint16_t flash_test_erases, flash_test_programs;
unsigned flash_test_request(unsigned key);
void flash_test_poll(uint32_t ms);
bool flash_test_read(unsigned offset,uint16_t *word);
