/* TC32 ELF GCC 4.5.1 的发行包没有 libgcc 的 64 位乘除入口。 */
#include <stdint.h>

uint64_t __muldi3(uint64_t left, uint64_t right)
{
    uint64_t product = 0;
    while (right) { /* 最多 64 次移位 */
        if (right & 1u) product += left;
        left <<= 1;
        right >>= 1;
    }
    return product;
}

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor)
{
    uint64_t quotient = 0, bit = 1;
    if (!divisor) return UINT64_MAX;
    while (divisor <= (dividend >> 1)) {
        divisor <<= 1;
        bit <<= 1;
    }
    while (bit) {
        if (dividend >= divisor) {
            dividend -= divisor;
            quotient |= bit;
        }
        divisor >>= 1;
        bit >>= 1;
    }
    return quotient;
}

int64_t __divdi3(int64_t dividend, int64_t divisor)
{
    uint64_t left = (uint64_t)dividend, right = (uint64_t)divisor;
    if (dividend < 0) left = 0u - left;
    if (divisor < 0) right = 0u - right;
    uint64_t result = __udivdi3(left, right);
    return (int64_t)(((dividend < 0) != (divisor < 0)) ? 0u - result : result);
}
