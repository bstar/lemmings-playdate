/*
 * Unlock and completion rules. See lp_progress.h.
 *
 * Pure functions over a bitfield the caller owns, with no I/O and no state,
 * which is what makes the unlock rule directly testable.
 */
#include "lp_progress.h"

int lp_progress_completed(const uint8_t completed[LP_COMPLETION_BYTES], unsigned index) {
    if (!completed || index >= LP_TOTAL_LEVELS) return 0;
    return !!(completed[index / 8u] & (uint8_t)(1u << (index & 7u)));
}

unsigned lp_progress_completed_count(const uint8_t completed[LP_COMPLETION_BYTES],
                                     unsigned rating) {
    unsigned count = 0, level;
    if (rating >= LP_RATING_COUNT) return 0;
    for (level = 0; level < LP_LEVELS_PER_RATING; ++level)
        count += (unsigned)lp_progress_completed(
            completed, rating * LP_LEVELS_PER_RATING + level);
    return count;
}

unsigned lp_progress_current(const uint8_t completed[LP_COMPLETION_BYTES], unsigned rating) {
    unsigned level;
    if (rating >= LP_RATING_COUNT) return 0;
    for (level = 0; level < LP_LEVELS_PER_RATING; ++level)
        if (!lp_progress_completed(completed, rating * LP_LEVELS_PER_RATING + level))
            return level;
    return LP_LEVELS_PER_RATING - 1u;
}

int lp_progress_can_select(const uint8_t completed[LP_COMPLETION_BYTES],
                           unsigned rating, unsigned level) {
    unsigned index;
    if (rating >= LP_RATING_COUNT || level >= LP_LEVELS_PER_RATING) return 0;
    index = rating * LP_LEVELS_PER_RATING + level;
    return lp_progress_completed(completed, index) ||
           level == lp_progress_current(completed, rating);
}

unsigned lp_progress_move(const uint8_t completed[LP_COMPLETION_BYTES],
                          unsigned rating, unsigned level, int direction) {
    int candidate;
    if (rating >= LP_RATING_COUNT || level >= LP_LEVELS_PER_RATING || !direction)
        return level;
    candidate = (int)level + (direction > 0 ? 1 : -1);
    while (candidate >= 0 && candidate < (int)LP_LEVELS_PER_RATING) {
        if (lp_progress_can_select(completed, rating, (unsigned)candidate))
            return (unsigned)candidate;
        candidate += direction > 0 ? 1 : -1;
    }
    return level;
}
