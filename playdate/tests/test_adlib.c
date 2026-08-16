#include "lp_adlib.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const uint8_t* expected;
    size_t size, position;
} TraceCheck;

static void check_write(void* context, uint32_t step, uint8_t reg, uint8_t value) {
    TraceCheck* check = context;
    uint8_t record[6] = {
        (uint8_t)step, (uint8_t)(step >> 8), (uint8_t)(step >> 16),
        (uint8_t)(step >> 24), reg, value
    };
    int i;
    assert(check->position + sizeof record <= check->size);
    for (i = 0; i < 6; ++i) assert(check->expected[check->position + i] == record[i]);
    check->position += sizeof record;
}

int main(int argc, char** argv) {
    uint8_t *image, *trace;
    int16_t samples[2048];
    LPAdlibPlayer player;
    FILE* file;
    long trace_size;
    uint32_t trace_steps;
    TraceCheck check;
    long long energy = 0;
    int peak = 0, audible = 0, block, i;
    assert(argc == 3);
    file = fopen(argv[1], "rb"); assert(file);
    image = malloc(LP_ADLIB_IMAGE_SIZE); assert(image);
    assert(fread(image, 1, LP_ADLIB_IMAGE_SIZE, file) == LP_ADLIB_IMAGE_SIZE);
    assert(fgetc(file) == EOF); fclose(file);
    file = fopen(argv[2], "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0); trace_size = ftell(file);
    assert(trace_size >= 8 && fseek(file, 0, SEEK_SET) == 0);
    trace = malloc((size_t)trace_size); assert(trace);
    assert(fread(trace, 1, (size_t)trace_size, file) == (size_t)trace_size); fclose(file);
    assert(trace[0] == 'L' && trace[1] == 'P' && trace[2] == 'R' && trace[3] == '1');
    trace_steps = (uint32_t)trace[4] | (uint32_t)trace[5] << 8 |
                  (uint32_t)trace[6] << 16 | (uint32_t)trace[7] << 24;
    check.expected = trace + 8; check.size = (size_t)trace_size - 8; check.position = 0;
    assert(lp_adlib_init(&player, image, LP_ADLIB_IMAGE_SIZE));
    lp_adlib_set_write_callback(&player, check_write, &check);
    assert(lp_adlib_play_music(&player, 5)); /* Verify the actual Fun 1 stream. */
    while (player.sequence_step < trace_steps) assert(lp_adlib_render(&player, samples, 1));
    if (check.position != check.size)
        fprintf(stderr, "register trace consumed %zu of %zu bytes at step %u\n",
                check.position, check.size, player.sequence_step);
    assert(check.position == check.size);
    lp_adlib_set_write_callback(&player, NULL, NULL);
    assert(lp_adlib_play_music(&player, 5)); /* Fun 1: Lemming 1 */
    for (block = 0; block < 260; ++block) {
        assert(lp_adlib_render(&player, samples, 512));
        for (i = 0; i < 512; ++i) {
            int sample = samples[i];
            int magnitude = sample < 0 ? -sample : sample;
            if (magnitude > peak) peak = magnitude;
            if (magnitude > 64) audible++;
            energy += (long long)sample * sample;
        }
    }
    printf("AdLib synth metrics: peak %d, audible %d, energy %lld\n", peak, audible, energy);
    fflush(stdout);
    assert(peak > 1000 && peak < 32768);
    assert(audible > 44100);
    assert(energy > 1000000000ll);
    assert(lp_adlib_play_music(&player, 5));
    assert(lp_adlib_pump(&player, 2048) == 2048);
    assert(lp_adlib_buffered(&player) == 2048);
    assert(lp_adlib_read_buffered(&player, samples, 512));
    assert(lp_adlib_buffered(&player) == 1536);
    assert(lp_adlib_underruns(&player) == 0);
    assert(lp_adlib_read_buffered(&player, samples, 2048));
    assert(lp_adlib_buffered(&player) == 0);
    assert(lp_adlib_underruns(&player) == 1);
    lp_adlib_stop(&player);
    assert(!lp_adlib_render(&player, samples, 512));
    free(trace); free(image);
    printf("AdLib synth test passed (peak %d, audible %d)\n", peak, audible);
    return 0;
}
