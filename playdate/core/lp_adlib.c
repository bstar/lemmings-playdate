/*
 * DOS AdLib driver. See lp_adlib.h for the threading and rate model.
 *
 * The DOS sound image holds a command stream per song, not audio. This file
 * interprets it: per-channel state advances on the driver's own tick, emitting
 * OPL2 register writes to lp_dbopl, which synthesizes.
 *
 * Layout: the OPL wrapper with its resampler and filter, then the per-channel
 * command interpreter, then the ring buffer that separates the producer from
 * the audio callback.
 *
 * Offsets into the sound image are facts about the original data. They are
 * named rather than inlined so the correspondence stays visible.
 */
#include "lp_adlib.h"
#include "lp_dbopl.h"

#include <string.h>

enum { LP_NONE, LP_MUSIC, LP_SOUND };

static uint32_t atomic_load(const volatile uint32_t* value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void atomic_store(volatile uint32_t* target, uint32_t value) {
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static uint16_t word(const uint8_t* image, unsigned offset) {
    return (uint16_t)(image[offset] | (uint16_t)image[offset + 1] << 8);
}

static void opl_write(LPOPL* opl, uint8_t reg, uint8_t value) {
    lp_dbopl_write(opl->context, reg, value);
}

static int16_t clamp16(int32_t value) {
    if (value < -32768) return -32768;
    if (value > 32767) return 32767;
    return (int16_t)value;
}

static int opl_init(LPOPL* opl) {
    void* context = opl->context;
    memset(opl, 0, sizeof *opl);
    if (context) {
        opl->context = context;
        lp_dbopl_reset(context, LP_ADLIB_NATIVE_RATE);
    } else {
        opl->context = lp_dbopl_create(LP_ADLIB_NATIVE_RATE);
    }
    return opl->context != NULL;
}

static void emit(LPAdlibPlayer* p, uint8_t reg, uint8_t value) {
    opl_write(&p->opl, reg, value);
    if (p->write_callback)
        p->write_callback(p->write_context, p->sequence_step, reg, value);
}

static void frequency(LPAdlibPlayer* p, LPAdlibChannel* c) {
    unsigned index = (c->note + c->transpose) & 0xFF;
    uint8_t octave = p->image[2727 + index + 4];
    uint8_t fi = p->image[2823 + index + 4];
    uint16_t f = word(p->image, 2343 + fi * 32);
    if (!(f & 0x8000)) octave--;
    if (octave & 0x80) { octave++; f <<= 1; }
    emit(p, c->channel + 0xA0, f & 0xFF);
    c->key_value = ((f >> 8) & 3) | (octave << 2);
    c->key_register = c->channel + 0xB0;
    emit(p, c->key_register, c->key_value | 0x20);
}

static void level(LPAdlibPlayer* p, LPAdlibChannel* c, uint16_t position) {
    uint8_t source = p->image[position];
    uint8_t value = p->image[2215 + (source & 0x7F)];
    value |= (p->image[c->instrument_record + 12] << 2) & 0xC0;
    emit(p, c->operator_a + 0x40, value);
    source = (c->level_delta + p->image[c->instrument_record + 10]) & 0x7F;
    value = p->image[2215 + source];
    value |= (p->image[c->instrument_record + 12] >> 2) & 0xC0;
    emit(p, c->operator_b + 0x40, value);
}

static void envelope(LPAdlibPlayer* p, LPAdlibChannel* c, uint8_t number) {
    static const uint8_t bases[9] = {0x60,0x60,0x80,0x80,0xE0,0xE0,0xC0,0x20,0x20};
    static const uint8_t offsets[9] = {0,1,2,3,6,7,9,4,5};
    static const uint8_t operators[9] = {0,1,0,1,0,1,2,0,1};
    unsigned i;
    uint16_t base = c->instrument_base + ((number - 1) << 4);
    c->instrument = number;
    for (i = 0; i < 9; ++i) {
        uint8_t op = operators[i] == 2 ? c->channel :
                     operators[i] ? c->operator_b : c->operator_a;
        emit(p, bases[i] + op, p->image[base + offsets[i]]);
    }
    c->transpose = p->image[base + 8]; c->level_delta = p->image[base + 11];
    c->instrument_record = base; level(p, c, base + 10);
}

static void advance_channel(LPAdlibPlayer* p, LPAdlibChannel* c) {
    uint16_t position, saved;
    if (c->state == LP_NONE) return;
    c->wait--; saved = c->position;
    if (c->wait) {
        if (c->slide) { c->note += c->slide; frequency(p, c); }
        if (p->image[saved] != 0x82 && p->image[c->instrument_record + 14] == c->wait) {
            emit(p, c->key_register, c->key_value); c->slide = 0;
        }
        return;
    }
    position = c->position;
    for (;;) {
        uint8_t command = p->image[position++];
        if (command < 0x80) {
            c->note = command; emit(p, c->key_register, c->key_value); frequency(p, c);
            c->wait = c->wait_sum; c->position = position; return;
        }
        if (command >= 0xE0) c->wait_sum = command - 0xDF;
        else if (command >= 0xC0) envelope(p, c, command - 0xC0);
        else if (command <= 0xB0) {
            switch (command & 15) {
                case 0: {
                    uint16_t target = word(p->image, c->program); c->program += 2;
                    if (!target) {
                        uint16_t table = word(p->image, c->program) + 2926;
                        position = word(p->image, table) + 2926; c->program = table + 2;
                    } else position = target + 2926;
                    c->position = position; break;
                }
                case 1: emit(p, c->key_register, c->key_value); c->slide = 0;
                        c->position = position; c->wait = c->wait_sum; return;
                case 2: c->position = position; c->wait = c->wait_sum; return;
                case 3: return;
                case 4: c->transpose = p->image[position++]; break;
                case 5: emit(p, c->key_register, c->key_value); c->state = LP_NONE; return;
                case 6: c->slide = 1; break;
                case 7: c->slide = 0xFF; break;
                case 8: level(p, c, position++); break;
                default: break;
            }
        } else level(p, c, position++);
    }
}

static void sequencer_step(LPAdlibPlayer* p) {
    unsigned i;
    if (p->cycle) { p->cycle--; p->sequence_step++; return; }
    p->cycle = p->wait_cycles;
    if (!p->initialized) {
        static const uint8_t init[][2] = {{4,0x60},{4,0x80},{2,0xFF},{4,0x21},{4,0x60},{4,0x80}};
        p->initialized = 1;
        for (i = 0; i < sizeof init / sizeof init[0]; ++i) emit(p, init[i][0], init[i][1]);
        for (i = 0; i < p->channel_count; ++i) emit(p, p->channels[i].key_register, p->channels[i].key_value);
        emit(p, 1, 0x20); emit(p, 0xBD, 0xC0); emit(p, 8, 0); emit(p, 4, 0x21);
    }
    for (i = 0; i < p->channel_count; ++i) advance_channel(p, &p->channels[i]);
    p->sequence_step++;
}

int lp_adlib_init(LPAdlibPlayer* p, const uint8_t* image, size_t size) {
    if (!p || !image || size != LP_ADLIB_IMAGE_SIZE) return 0;
    memset(p, 0, sizeof *p); p->image = image; return opl_init(&p->opl);
}

static int begin_music(LPAdlibPlayer* p, unsigned track) {
    unsigned i;
    uint16_t header, instrument_base;
    uint32_t reference_tick, scaled_tick;
    if (!p || !p->image) return 0;
    track %= 21; header = word(p->image, 2926 + track * 2);
    p->sample_rate_factor = word(p->image, header);
    instrument_base = word(p->image, header + 2) + 2926;
    p->wait_cycles = p->image[header + 4]; p->channel_count = p->image[header + 5];
    if (!p->sample_rate_factor || p->channel_count > 9) return 0;
    if (!opl_init(&p->opl)) return 0;
    memset(p->channels, 0, sizeof p->channels);
    for (i = 0; i < p->channel_count; ++i) {
        LPAdlibChannel* c = &p->channels[i];
        unsigned q = 1452 + i * 20;
        c->note = p->image[q]; c->wait = 1; c->instrument_record = word(p->image, q + 2);
        c->instrument = p->image[q + 4]; c->operator_a = p->image[q + 5];
        c->operator_b = p->image[q + 6]; c->channel = p->image[q + 7];
        c->key_value = p->image[q + 8]; c->key_register = p->image[q + 9];
        c->program = word(p->image, header + 6 + i * 2) + 2926;
        c->position = word(p->image, c->program) + 2926; c->program += 2;
        c->level_delta = p->image[q + 15]; c->wait_sum = p->image[q + 17];
        c->transpose = p->image[q + 18]; c->slide = p->image[q + 19];
        c->instrument_base = instrument_base; c->state = LP_MUSIC;
    }
    p->cycle = 0; p->initialized = 0; p->sequence_step = 0;
    /* Preserve the approved renderer's rounded 49,716 Hz tick duration even
       when the physical-device backend runs at a lower synthesis rate. */
    reference_tick = (LP_ADLIB_REFERENCE_RATE * 210u +
                      p->sample_rate_factor / 2u) / p->sample_rate_factor;
    scaled_tick = reference_tick * LP_ADLIB_NATIVE_RATE;
    p->samples_per_tick = scaled_tick / LP_ADLIB_REFERENCE_RATE;
    p->tick_remainder = scaled_tick % LP_ADLIB_REFERENCE_RATE;
    p->tick_phase = 0;
    p->native_until_step = 0;
    return 1;
}

int lp_adlib_play_music(LPAdlibPlayer* p, unsigned track) {
    uint32_t write;
    if (!p || !p->image) return 0;
    /* Discard queued audio from the previous song. The consumer uses a CAS
       when publishing progress, so it cannot undo this flush. */
    write = atomic_load(&p->ring_write);
    atomic_store(&p->ring_read, write);
    p->pending_track = (uint8_t)(track % 21); p->request = 1; p->active = 1;
    return 1;
}

void lp_adlib_stop(LPAdlibPlayer* p) {
    if (p) {
        p->active = 0; p->request = 0;
        atomic_store(&p->ring_read, atomic_load(&p->ring_write));
    }
}

static void fill_native(LPAdlibPlayer* p) {
    int32_t raw[512];
    uint32_t count, i;
    if (!p->native_until_step) {
        sequencer_step(p);
        p->native_until_step = p->samples_per_tick;
        p->tick_phase += p->tick_remainder;
        if (p->tick_phase >= LP_ADLIB_REFERENCE_RATE) {
            p->tick_phase -= LP_ADLIB_REFERENCE_RATE;
            p->native_until_step++;
        }
    }
    count = p->native_until_step;
    if (count > 512u) count = 512u;
    lp_dbopl_generate(p->opl.context, raw, (int)count);
    for (i = 0; i < count; ++i) {
        p->opl.native[i] = clamp16(raw[i]);
    }
    p->native_until_step -= count;
    p->opl.native_position = 0;
    p->opl.native_count = (uint16_t)count;
}

static int16_t native_sample(LPAdlibPlayer* p) {
    if (p->opl.native_position == p->opl.native_count) fill_native(p);
    return p->opl.native[p->opl.native_position++];
}

static int16_t output_sample(LPAdlibPlayer* p) {
    int32_t value;
    if (!p->opl.resample_ready) {
        p->opl.source_a = native_sample(p);
        p->opl.source_b = native_sample(p);
        p->opl.resample_ready = 1;
    }
    value = p->opl.source_a +
        ((int32_t)(p->opl.source_b - p->opl.source_a) *
         (int32_t)p->opl.resample_phase) / (int32_t)LP_ADLIB_OUTPUT_RATE;
    p->opl.resample_phase += LP_ADLIB_NATIVE_RATE;
    while (p->opl.resample_phase >= LP_ADLIB_OUTPUT_RATE) {
        p->opl.resample_phase -= LP_ADLIB_OUTPUT_RATE;
        p->opl.source_a = p->opl.source_b;
        p->opl.source_b = native_sample(p);
    }
    /* Filtering after rate conversion keeps the response identical at the
       44.1 kHz output rate in both Simulator and device builds. */
    p->opl.filter_a += ((value - p->opl.filter_a) * 2) / 3;
    p->opl.filter_b += ((p->opl.filter_a - p->opl.filter_b) * 2) / 3;
    return clamp16(p->opl.filter_b);
}

int lp_adlib_render(LPAdlibPlayer* p, int16_t* mono, int frames) {
    int i;
    if (!p || !mono || frames <= 0 || !p->active) return 0;
    if (p->request) {
        unsigned track = p->pending_track;
        p->request = 0;
        if (!begin_music(p, track)) { p->active = 0; return 0; }
    }
    for (i = 0; i < frames; ++i) mono[i] = output_sample(p);
    return 1;
}

uint32_t lp_adlib_buffered(const LPAdlibPlayer* p) {
    uint32_t read, write;
    if (!p) return 0;
    read = atomic_load(&p->ring_read);
    write = atomic_load(&p->ring_write);
    return write - read;
}

uint32_t lp_adlib_underruns(const LPAdlibPlayer* p) {
    return p ? atomic_load(&p->underruns) : 0;
}

int lp_adlib_pump(LPAdlibPlayer* p, int max_frames) {
    int16_t block[256];
    uint32_t read, write, buffered, wanted, produced = 0;
    if (!p || !p->active || max_frames <= 0) return 0;
    read = atomic_load(&p->ring_read);
    write = atomic_load(&p->ring_write);
    buffered = write - read;
    if (buffered >= LP_ADLIB_RING_TARGET) return 0;
    wanted = LP_ADLIB_RING_TARGET - buffered;
    if (wanted > (uint32_t)max_frames) wanted = (uint32_t)max_frames;
    while (produced < wanted) {
        uint32_t count = wanted - produced, i;
        if (count > 256u) count = 256u;
        if (!lp_adlib_render(p, block, (int)count)) break;
        for (i = 0; i < count; ++i)
            p->ring[(write + produced + i) & (LP_ADLIB_RING_FRAMES - 1u)] = block[i];
        produced += count;
    }
    /* Publish only fully rendered frames. */
    atomic_store(&p->ring_write, write + produced);
    return (int)produced;
}

int lp_adlib_read_buffered(LPAdlibPlayer* p, int16_t* mono, int frames) {
    uint32_t start, read, write, available, count, i;
    if (!p || !mono || frames <= 0 || !p->active) return 0;
    start = read = atomic_load(&p->ring_read);
    write = atomic_load(&p->ring_write);
    available = write - read;
    count = available < (uint32_t)frames ? available : (uint32_t)frames;
    for (i = 0; i < count; ++i)
        mono[i] = p->ring[(read + i) & (LP_ADLIB_RING_FRAMES - 1u)];
    if (count < (uint32_t)frames) {
        memset(mono + count, 0, ((uint32_t)frames - count) * sizeof *mono);
        __atomic_add_fetch(&p->underruns, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&p->underrun_frames,
                           (uint32_t)frames - count, __ATOMIC_RELAXED);
    }
    read += count;
    /* A concurrent track-change flush wins over this consumer update. */
    __atomic_compare_exchange_n(&p->ring_read, &start, read, 0,
                                __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    return 1;
}

void lp_adlib_set_write_callback(LPAdlibPlayer* p,
    void (*callback)(void*, uint32_t, uint8_t, uint8_t), void* context) {
    if (p) { p->write_callback = callback; p->write_context = context; }
}
