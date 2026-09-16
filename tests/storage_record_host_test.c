#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "storage_record.h"

#define MOCK_ERASE_SIZE 4096u
#define MOCK_SECTORS    4u
#define MOCK_FLASH_SIZE (MOCK_ERASE_SIZE * MOCK_SECTORS)
#define PAYLOAD_SIZE    24u
#define TEST_MAGIC      0x53544131u

static uint8_t s_flash[MOCK_FLASH_SIZE];
static int s_fail_program_after = -1;
static int s_program_calls;

static int mock_begin(void *ctx) { (void)ctx; return 1; }
static void mock_end(void *ctx) { (void)ctx; }

static int mock_read(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    if ((addr > MOCK_FLASH_SIZE) || (len > (MOCK_FLASH_SIZE - addr))) return 0;
    memcpy(buf, &s_flash[addr], len);
    return 1;
}

static int mock_program(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    (void)ctx;
    if ((addr > MOCK_FLASH_SIZE) || (len > (MOCK_FLASH_SIZE - addr))) return 0;
    if ((s_fail_program_after >= 0) && (s_program_calls++ >= s_fail_program_after)) return 0;
    for (i = 0u; i < len; ++i) {
        if ((uint8_t)(s_flash[addr + i] | buf[i]) != s_flash[addr + i]) return 0;
        s_flash[addr + i] &= buf[i];
    }
    return 1;
}

static int mock_erase(void *ctx, uint32_t addr, uint32_t len)
{
    (void)ctx;
    if ((len != MOCK_ERASE_SIZE) || ((addr % MOCK_ERASE_SIZE) != 0u) ||
        (addr > MOCK_FLASH_SIZE) || (len > (MOCK_FLASH_SIZE - addr))) return 0;
    memset(&s_flash[addr], 0xFF, len);
    return 1;
}

static const storage_port_t s_port = {
    0, MOCK_ERASE_SIZE, 4u, 0xFFu,
    mock_begin, mock_end, mock_read, mock_program, mock_erase,
};

static storage_region_t test_region(void)
{
    storage_region_t region;
    region.base = 0u;
    region.size = MOCK_FLASH_SIZE;
    return region;
}

static void fill_payload(uint8_t *payload, uint32_t value)
{
    uint32_t i;
    for (i = 0u; i < PAYLOAD_SIZE; ++i) payload[i] = (uint8_t)(value + i);
}

static void reopen_and_expect(const uint8_t *expected)
{
    storage_record_store_t store;
    uint8_t actual[PAYLOAD_SIZE];
    assert(storage_record_open(&store, &s_port, test_region(), TEST_MAGIC, 1u, PAYLOAD_SIZE));
    assert(storage_record_load(&store, actual));
    assert(memcmp(actual, expected, PAYLOAD_SIZE) == 0);
}

static void test_interrupted_write(int fail_after)
{
    storage_record_store_t store;
    uint8_t old_payload[PAYLOAD_SIZE];
    uint8_t new_payload[PAYLOAD_SIZE];

    memset(s_flash, 0xFF, sizeof(s_flash));
    fill_payload(old_payload, 0x10u);
    fill_payload(new_payload, 0x80u);
    assert(storage_record_open(&store, &s_port, test_region(), TEST_MAGIC, 1u, PAYLOAD_SIZE));
    assert(storage_record_save(&store, old_payload));

    s_program_calls = 0;
    s_fail_program_after = fail_after;
    assert(!storage_record_save(&store, new_payload));
    s_fail_program_after = -1;
    reopen_and_expect(old_payload);
}

static void test_sector_rotation_power_loss(void)
{
    storage_record_store_t store;
    uint8_t payload[PAYLOAD_SIZE];
    uint8_t last_good[PAYLOAD_SIZE];
    uint16_t i;
    uint16_t slots_per_sector;

    memset(s_flash, 0xFF, sizeof(s_flash));
    assert(storage_record_open(&store, &s_port, test_region(), TEST_MAGIC, 1u, PAYLOAD_SIZE));
    slots_per_sector = store.slots_per_sector;
    assert(slots_per_sector > 2u);
    for (i = 0u; i < slots_per_sector; ++i) {
        fill_payload(payload, (uint32_t)(i + 1u));
        assert(storage_record_save(&store, payload));
        memcpy(last_good, payload, sizeof(last_good));
    }

    fill_payload(payload, 0xD0u);
    s_program_calls = 0;
    s_fail_program_after = 0;
    assert(!storage_record_save(&store, payload));
    s_fail_program_after = -1;
    reopen_and_expect(last_good);

    assert(storage_record_open(&store, &s_port, test_region(), TEST_MAGIC, 1u, PAYLOAD_SIZE));
    assert(storage_record_load(&store, last_good));
    assert(storage_record_save(&store, payload));
    reopen_and_expect(payload);
}

int main(void)
{
    storage_record_store_t store;
    uint8_t p0[PAYLOAD_SIZE];
    uint8_t p1[PAYLOAD_SIZE];
    uint8_t out[PAYLOAD_SIZE];

    memset(s_flash, 0xFF, sizeof(s_flash));
    fill_payload(p0, 1u);
    fill_payload(p1, 33u);
    assert(storage_record_open(&store, &s_port, test_region(), TEST_MAGIC, 1u, PAYLOAD_SIZE));
    assert(!storage_record_load(&store, out));
    assert(storage_record_save(&store, p0));
    assert(storage_record_save(&store, p1));
    reopen_and_expect(p1);

    test_interrupted_write(0);
    test_interrupted_write(1);
    test_interrupted_write(2);
    test_sector_rotation_power_loss();

    puts("storage_record_host_test: OK");
    return 0;
}
