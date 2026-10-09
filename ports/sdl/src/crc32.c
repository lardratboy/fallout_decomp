/* CRC-32 (IEEE 802.3, as zlib's crc32()), so the port needs no zlib.
   The replay trace hashes the whole frame buffer on every get_ticks() call,
   so speed matters: ARMv8 has instructions for exactly this polynomial;
   elsewhere a slicing-by-8 table is used. */
#include <stdint.h>
#include <string.h>
#include "machine.h"

#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>

unsigned long crc32_buf(const unsigned char *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    uint64_t v;

    for (; n >= 8; n -= 8, p += 8) {
        memcpy(&v, p, 8);
        c = __crc32d(c, v);
    }
    while (n--)
        c = __crc32b(c, *p++);
    return c ^ 0xFFFFFFFFu;
}

#else

static uint32_t table[8][256];

static void make_table(void)
{
    uint32_t c;
    int i, k;

    for (i = 0; i < 256; i++) {
        c = (uint32_t)i;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[0][i] = c;
    }
    for (i = 0; i < 256; i++)
        for (k = 1; k < 8; k++)
            table[k][i] = (table[k - 1][i] >> 8) ^ table[0][table[k - 1][i] & 0xFF];
}

unsigned long crc32_buf(const unsigned char *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu, lo, hi;

    if (table[0][1] == 0)
        make_table();
    for (; n >= 8; n -= 8, p += 8) {
        lo = c ^ ((uint32_t)p[0] | (uint32_t)p[1] << 8 |
                  (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
        hi = (uint32_t)p[4] | (uint32_t)p[5] << 8 |
             (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
        c = table[7][lo & 0xFF] ^ table[6][(lo >> 8) & 0xFF] ^
            table[5][(lo >> 16) & 0xFF] ^ table[4][lo >> 24] ^
            table[3][hi & 0xFF] ^ table[2][(hi >> 8) & 0xFF] ^
            table[1][(hi >> 16) & 0xFF] ^ table[0][hi >> 24];
    }
    while (n--)
        c = table[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

#endif
