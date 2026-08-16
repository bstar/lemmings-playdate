/*
 * lp_dbopl: C entry points for the DBOPL OPL2 synthesizer.
 *
 * DBOPL is C++ and comes from DOSBox. The engine is C, so this header exposes
 * the few operations it needs behind an opaque context. lp_dbopl.cpp holds the
 * thin wrapper, and dbopl_core.cpp compiles the upstream source unmodified.
 *
 * lp_adlib drives this: it interprets the DOS command stream and writes the
 * resulting register changes here, then pulls synthesized samples out.
 *
 * Licensing: DBOPL is GPL-2.0-or-later, unlike the rest of this project. Any
 * binary linking it inherits that. Replacing the backend means reimplementing
 * these five functions; nothing above this header depends on DBOPL itself.
 */
#ifndef LP_DBOPL_H
#define LP_DBOPL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate a synthesizer running at `sample_rate`. NULL on failure. */
void* lp_dbopl_create(uint32_t sample_rate);
void lp_dbopl_destroy(void* context);

/* Return to power-on register state, optionally at a new rate. */
void lp_dbopl_reset(void* context, uint32_t sample_rate);

/* Write one OPL2 register, the same write the DOS driver would have made. */
void lp_dbopl_write(void* context, uint8_t reg, uint8_t value);

/* Synthesize `frames` mono samples. Output is wider than 16 bits so the
   caller controls scaling and clipping rather than inheriting DOSBox's
   mixer behaviour. */
void lp_dbopl_generate(void* context, int32_t* mono, int frames);

#ifdef __cplusplus
}
#endif

#endif
