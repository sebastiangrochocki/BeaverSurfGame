/* Eight-byte cartridge record: "BVR1", 24-bit little-endian best, wrapping-sum checksum. */
#include "rules.h"

static uint8_t checksum(const uint8_t *bytes) {
    uint8_t sum = 0;
    uint8_t i;
    for (i = 0; i < 7; i++) sum = (uint8_t)(sum + bytes[i]);
    return sum;
}

void save_encode(uint32_t best, uint8_t *out) {
    if (best > 0x00FFFFFFUL) best = 0x00FFFFFFUL;
    out[0] = 'B';
    out[1] = 'V';
    out[2] = 'R';
    out[3] = '1';
    out[4] = (uint8_t)(best & 0xFF);
    out[5] = (uint8_t)((best >> 8) & 0xFF);
    out[6] = (uint8_t)((best >> 16) & 0xFF);
    out[7] = checksum(out);
}

uint8_t save_decode(const uint8_t *in, uint32_t *best) {
    if (in[0] != 'B' || in[1] != 'V' || in[2] != 'R' || in[3] != '1') return 0;
    if (in[7] != checksum(in)) return 0;
    *best = (uint32_t)in[4] | ((uint32_t)in[5] << 8) | ((uint32_t)in[6] << 16);
    return 1;
}
