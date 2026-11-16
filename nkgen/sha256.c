#include "internal.h"

#define PROGRESS_STEP 0x100000u

static const uint32_t round_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t rotate_right(uint32_t value, uint32_t count)
{
    return value >> count | value << (32 - count);
}

static void compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t schedule[64];
    for (uint32_t index = 0; index < 16; index++) {
        const uint8_t *word = block + index * 4;
        schedule[index] = (uint32_t)word[0] << 24 | (uint32_t)word[1] << 16 | (uint32_t)word[2] << 8 | word[3];
    }
    for (uint32_t index = 16; index < 64; index++) {
        uint32_t early = schedule[index - 15];
        uint32_t late = schedule[index - 2];
        uint32_t sigma0 = rotate_right(early, 7) ^ rotate_right(early, 18) ^ early >> 3;
        uint32_t sigma1 = rotate_right(late, 17) ^ rotate_right(late, 19) ^ late >> 10;
        schedule[index] = schedule[index - 16] + sigma0 + schedule[index - 7] + sigma1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (uint32_t index = 0; index < 64; index++) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first = h + sum1 + choose + round_constants[index] + schedule[index];
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t second = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256(const uint8_t *data, uint32_t length, uint8_t digest[32], const char *stage)
{
    uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    uint32_t offset = 0;
    while (length - offset >= 64) {
        compress(state, data + offset);
        offset += 64;
        if (stage && offset % PROGRESS_STEP == 0) report_progress(stage, offset / PROGRESS_STEP, length / PROGRESS_STEP + 1);
    }
    uint8_t tail[128];
    uint32_t remaining = length - offset;
    fill_bytes(tail, 0, sizeof(tail));
    copy_bytes(tail, data + offset, remaining);
    tail[remaining] = 0x80;
    uint32_t tail_length = remaining < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)length * 8;
    for (uint32_t index = 0; index < 8; index++) tail[tail_length - 1 - index] = (uint8_t)(bits >> (index * 8));
    compress(state, tail);
    if (tail_length == 128) compress(state, tail + 64);
    for (uint32_t index = 0; index < 8; index++) {
        digest[index * 4] = (uint8_t)(state[index] >> 24);
        digest[index * 4 + 1] = (uint8_t)(state[index] >> 16);
        digest[index * 4 + 2] = (uint8_t)(state[index] >> 8);
        digest[index * 4 + 3] = (uint8_t)state[index];
    }
    if (stage) report_progress(stage, length / PROGRESS_STEP + 1, length / PROGRESS_STEP + 1);
}
