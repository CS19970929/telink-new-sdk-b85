#include <assert.h>
#include <stdint.h>
#include <stdio.h>
uint64_t __udivdi3(uint64_t, uint64_t);
uint64_t __muldi3(uint64_t, uint64_t);
int64_t __divdi3(int64_t, int64_t);
int main(void)
{
    uint64_t state = 71, divisor;
    unsigned i;
    for (i = 0; i < 3600; ++i) {
        state = state * UINT64_C(6364136223846793005) + 1;
        divisor = (state >> (i % 64)) | 1;
        assert(__muldi3(state, divisor) == state * divisor);
        assert(__udivdi3(state, divisor) == state / divisor);
        assert(__divdi3((int64_t)state, 32000) == (int64_t)state / 32000);
    }
    assert(__divdi3(INT64_MIN, 1) == INT64_MIN);
    assert(__divdi3(1, INT64_MIN) == 0);
    puts("PASS tc32 wide arithmetic");
    return 0;
}
