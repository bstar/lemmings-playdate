#include "lp_dbopl.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t u32le(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t hash_byte(uint64_t hash, uint8_t value) {
    return (hash ^ value) * UINT64_C(1099511628211);
}

int main(int argc, char** argv) {
    const uint64_t expected_hash = UINT64_C(0x79358478ee30ec6d);
    uint8_t* trace;
    int32_t samples[512];
    FILE* file;
    long size;
    uint32_t steps, factor, sample_count, rate, samples_per_tick;
    uint32_t step, position = 0;
    size_t record = 20;
    uint64_t hash = UINT64_C(14695981039346656037);
    void* opl;
    assert(argc == 2 || argc == 3);
    file = fopen(argv[1], "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0); size = ftell(file);
    assert(size >= 20 && fseek(file, 0, SEEK_SET) == 0);
    trace = static_cast<uint8_t*>(malloc((size_t)size)); assert(trace);
    assert(fread(trace, 1, (size_t)size, file) == (size_t)size); fclose(file);
    assert(trace[0] == 'L' && trace[1] == 'P' && trace[2] == 'R' && trace[3] == '3');
    steps = u32le(trace + 4); factor = u32le(trace + 8);
    sample_count = u32le(trace + 12); rate = u32le(trace + 16);
    assert(sample_count == 49716 && rate == 49716);
    samples_per_tick = (rate * 210u + factor / 2u) / factor;
    opl = lp_dbopl_create(rate); assert(opl);
    for (step = 0; step < steps && position < sample_count; ++step) {
        while (record + 6 <= (size_t)size && u32le(trace + record) == step) {
            lp_dbopl_write(opl, trace[record + 4], trace[record + 5]);
            record += 6;
        }
        uint32_t count = samples_per_tick;
        if (count > sample_count - position) count = sample_count - position;
        for (uint32_t generated = 0; generated < count;) {
            uint32_t chunk = count - generated;
            if (chunk > 512) chunk = 512;
            lp_dbopl_generate(opl, samples, (int)chunk);
            for (uint32_t i = 0; i < chunk; ++i) {
                int32_t value = samples[i];
                if (value < -32768) value = -32768;
                if (value > 32767) value = 32767;
                uint16_t bits = (uint16_t)(int16_t)value;
                hash = hash_byte(hash, (uint8_t)bits);
                hash = hash_byte(hash, (uint8_t)(bits >> 8));
            }
            generated += chunk;
        }
        position += count;
    }
    assert(record == (size_t)size && position == sample_count);
    if (argc == 2) assert(hash == expected_hash);
    lp_dbopl_destroy(opl); free(trace);
    printf("DBOPL oracle test passed (FNV-1a %016llx)\n",
           (unsigned long long)hash);
    return 0;
}
