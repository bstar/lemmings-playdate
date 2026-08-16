#ifndef LP_PROGRESS_H
#define LP_PROGRESS_H

#include <stdint.h>

#define LP_RATING_COUNT 4u
#define LP_LEVELS_PER_RATING 30u
#define LP_TOTAL_LEVELS (LP_RATING_COUNT * LP_LEVELS_PER_RATING)
#define LP_COMPLETION_BYTES ((LP_TOTAL_LEVELS + 7u) / 8u)

int lp_progress_completed(const uint8_t completed[LP_COMPLETION_BYTES], unsigned index);
unsigned lp_progress_completed_count(const uint8_t completed[LP_COMPLETION_BYTES],
                                     unsigned rating);
unsigned lp_progress_current(const uint8_t completed[LP_COMPLETION_BYTES], unsigned rating);
int lp_progress_can_select(const uint8_t completed[LP_COMPLETION_BYTES],
                           unsigned rating, unsigned level);
unsigned lp_progress_move(const uint8_t completed[LP_COMPLETION_BYTES],
                          unsigned rating, unsigned level, int direction);

#endif
