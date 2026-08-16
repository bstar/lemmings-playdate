#include "lp_progress.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void complete(uint8_t bits[LP_COMPLETION_BYTES], unsigned index) {
    bits[index / 8u] |= (uint8_t)(1u << (index & 7u));
}

int main(void) {
    uint8_t bits[LP_COMPLETION_BYTES];
    unsigned rating, level;
    memset(bits, 0, sizeof bits);

    for (rating = 0; rating < LP_RATING_COUNT; ++rating) {
        assert(lp_progress_current(bits, rating) == 0);
        assert(lp_progress_completed_count(bits, rating) == 0);
        assert(lp_progress_can_select(bits, rating, 0));
        assert(!lp_progress_can_select(bits, rating, 1));
        assert(lp_progress_move(bits, rating, 0, 1) == 0);
    }

    complete(bits, 0);
    complete(bits, 1);
    assert(lp_progress_current(bits, 0) == 2);
    assert(lp_progress_completed_count(bits, 0) == 2);
    assert(lp_progress_can_select(bits, 0, 0));
    assert(lp_progress_can_select(bits, 0, 2));
    assert(!lp_progress_can_select(bits, 0, 3));
    assert(lp_progress_move(bits, 0, 1, 1) == 2);
    assert(lp_progress_move(bits, 0, 0, -1) == 0);

    /* Old or hand-edited saves may contain gaps. Completed cards remain
       replayable, but locked cards between them are skipped. */
    complete(bits, 5);
    assert(lp_progress_current(bits, 0) == 2);
    assert(lp_progress_can_select(bits, 0, 5));
    assert(lp_progress_move(bits, 0, 2, 1) == 5);
    assert(lp_progress_move(bits, 0, 5, -1) == 2);

    memset(bits, 0, sizeof bits);
    for (level = 0; level < LP_LEVELS_PER_RATING; ++level) complete(bits, 60u + level);
    assert(lp_progress_completed_count(bits, 2) == LP_LEVELS_PER_RATING);
    assert(lp_progress_current(bits, 2) == LP_LEVELS_PER_RATING - 1u);
    for (level = 0; level < LP_LEVELS_PER_RATING; ++level)
        assert(lp_progress_can_select(bits, 2, level));
    assert(lp_progress_move(bits, 2, 29, 1) == 29);

    assert(!lp_progress_completed(bits, LP_TOTAL_LEVELS));
    assert(!lp_progress_can_select(bits, LP_RATING_COUNT, 0));
    puts("progress tests passed");
    return 0;
}
