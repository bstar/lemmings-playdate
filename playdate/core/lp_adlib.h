/*
 * lp_adlib: plays the DOS AdLib score in real time.
 *
 * The original music is not audio. It is a command stream inside ADLIB.DAT
 * that a driver interprets into OPL2 register writes. This module reimplements
 * that driver and feeds the writes to lp_dbopl, so songs repeat through their
 * own control flow, including independently looping voices, with no stored PCM
 * and no seams.
 *
 * Threading: gameplay code calls lp_adlib_pump from the main loop to
 * synthesize ahead into a ring buffer, and the audio callback calls
 * lp_adlib_read_buffered to copy out. The callback therefore never
 * synthesizes, allocates, or touches files, and a late frame costs a
 * zero-filled underrun rather than a stall. The indices between them are the
 * only shared state.
 *
 * Rates: synthesis on device runs above the final passband and interpolates up
 * to the output rate, because the Cortex-M7 cannot sustain the full reference
 * rate alongside gameplay. Host and Simulator builds synthesize at the
 * reference rate, which is what the byte-exact oracle tests compare against.
 */
#ifndef LP_ADLIB_H
#define LP_ADLIB_H

#include <stddef.h>
#include <stdint.h>

#define LP_ADLIB_IMAGE_SIZE 22125
#define LP_ADLIB_REFERENCE_RATE 49716u
#ifdef TARGET_PLAYDATE
/* The Cortex-M7 cannot sustain the full reference-rate DBOPL core alongside
   gameplay. Synthesize above the final 8.5 kHz passband and interpolate 2x. */
#define LP_ADLIB_NATIVE_RATE 22050u
#else
#define LP_ADLIB_NATIVE_RATE LP_ADLIB_REFERENCE_RATE
#endif
#define LP_ADLIB_OUTPUT_RATE 44100u
#define LP_ADLIB_RING_FRAMES 8192u
#define LP_ADLIB_RING_TARGET 6144u
#define LP_ADLIB_PUMP_FRAMES 2048u

typedef struct {
    void* context;
    int32_t filter_a, filter_b;
    int16_t native[512];
    uint16_t native_position, native_count;
    int16_t source_a, source_b;
    uint32_t resample_phase;
    uint8_t resample_ready;
} LPOPL;

typedef struct {
    uint8_t note, wait, instrument, operator_a, operator_b, channel;
    uint8_t key_value, key_register, level_delta, state, wait_sum, transpose, slide;
    uint16_t instrument_record, program, position, instrument_base;
} LPAdlibChannel;

typedef struct {
    const uint8_t* image;
    LPOPL opl;
    LPAdlibChannel channels[9];
    uint32_t samples_per_tick, tick_remainder, tick_phase, native_until_step;
    uint16_t sample_rate_factor;
    uint8_t channel_count, wait_cycles, cycle, initialized;
    volatile uint8_t active, request, pending_track;
    int16_t ring[LP_ADLIB_RING_FRAMES];
    volatile uint32_t ring_read, ring_write;
    volatile uint32_t underruns, underrun_frames;
    uint32_t sequence_step;
    void (*write_callback)(void* context, uint32_t step, uint8_t reg, uint8_t value);
    void* write_context;
} LPAdlibPlayer;

/* The caller retains ownership of the 22,125-byte decompressed DOS sound image. */
int lp_adlib_init(LPAdlibPlayer* player, const uint8_t* image, size_t size);

/* Start a song by its index within the sound image. Safe to call while
   another is playing; the request is picked up by the producer. */
int lp_adlib_play_music(LPAdlibPlayer* player, unsigned track);

/* Silence playback and reset the synthesizer. */
void lp_adlib_stop(LPAdlibPlayer* player);

/* Synthesize directly into the caller's buffer, bypassing the ring. Used by
   offline rendering and the oracle tests, not by real-time playback. */
int lp_adlib_render(LPAdlibPlayer* player, int16_t* mono, int frames);

/* Main-thread producer and audio-thread consumer for deadline-safe playback. */

/* Synthesize ahead into the ring, up to `max_frames`. Call every frame from
   the main loop. Returns frames produced. */
int lp_adlib_pump(LPAdlibPlayer* player, int max_frames);

/* Copy buffered samples out, from the audio callback. Any shortfall is
   zero-filled and counted as an underrun rather than waited on. */
int lp_adlib_read_buffered(LPAdlibPlayer* player, int16_t* mono, int frames);

/* Frames currently buffered, for deciding how hard to pump. */
uint32_t lp_adlib_buffered(const LPAdlibPlayer* player);

/* Cumulative underruns, which is the signal that the producer is late. */
uint32_t lp_adlib_underruns(const LPAdlibPlayer* player);

/* Observe every register write with its sequence step. Used by the tests to
   compare this driver against an independent reference; not needed to play. */
void lp_adlib_set_write_callback(LPAdlibPlayer* player,
    void (*callback)(void*, uint32_t, uint8_t, uint8_t), void* context);

#endif
