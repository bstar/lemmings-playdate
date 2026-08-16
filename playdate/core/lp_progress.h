/*
 * lp_progress: which levels the player has finished, and which may be chosen.
 *
 * Progress is a bitfield of LP_TOTAL_LEVELS bits, one per level, indexed as
 * rating * LP_LEVELS_PER_RATING + level. The platform adapter owns the storage
 * and persists it; these functions only interpret it, so they are pure and
 * directly testable.
 *
 * The unlock rule is that every rating is open from the start, and within a
 * rating the player may replay anything already completed plus the first
 * level not yet completed.
 */
#ifndef LP_PROGRESS_H
#define LP_PROGRESS_H

#include <stdint.h>

#define LP_RATING_COUNT 4u
#define LP_LEVELS_PER_RATING 30u
#define LP_TOTAL_LEVELS (LP_RATING_COUNT * LP_LEVELS_PER_RATING)
#define LP_COMPLETION_BYTES ((LP_TOTAL_LEVELS + 7u) / 8u)

/* Nonzero if the level at a flat 0..LP_TOTAL_LEVELS-1 index is complete. */
int lp_progress_completed(const uint8_t completed[LP_COMPLETION_BYTES], unsigned index);

/* How many levels of one rating are complete, for a progress readout. */
unsigned lp_progress_completed_count(const uint8_t completed[LP_COMPLETION_BYTES],
                                     unsigned rating);

/* The first incomplete level of a rating, which is the furthest the player may
   currently reach. Returns the last level once the rating is finished. */
unsigned lp_progress_current(const uint8_t completed[LP_COMPLETION_BYTES], unsigned rating);

/* Nonzero if the player may start this level: already completed, or the
   current one. */
int lp_progress_can_select(const uint8_t completed[LP_COMPLETION_BYTES],
                           unsigned rating, unsigned level);

/* Step to the next selectable level in `direction`, skipping locked ones.
   Returns `level` unchanged when there is nothing to move to, so a caller can
   detect the end of the run by comparing against what it passed in. */
unsigned lp_progress_move(const uint8_t completed[LP_COMPLETION_BYTES],
                          unsigned rating, unsigned level, int direction);

#endif
