/*
 * Level to song mapping and loop points. See lp_music.h.
 *
 * The mapping follows the original rotation rather than a formula, so it is a
 * table. Loop boundaries were measured from the DOS command stream and are
 * stored as seconds for the platform layer to seek with.
 */
#include "lp_music.h"

unsigned lp_music_for_level(unsigned level_index) {
    /* DOS success-order rotation:
       Lemming1/2/3, Mountain, Ten Lemmings, Can-Can, Tim1..4, Doggie,
       Tim5..10. The four special-graphics levels replace the rotation. */
    static const unsigned cycle[17] = {
        5, 6, 7, 9, 10, 3, 11, 12, 13, 14, 4, 15, 16, 17, 18, 19, 20
    };
    if (level_index == 21) return 1;  /* Fun 22: Beast I */
    if (level_index == 43) return 8;  /* Tricky 14: Menace */
    if (level_index == 74) return 0;  /* Taxing 15: Awesome */
    if (level_index == 111) return 2; /* Mayhem 22: Beast II */
    return cycle[level_index % 17];
}

void lp_music_loop_range(unsigned track, float* start_seconds, float* end_seconds) {
    typedef struct { unsigned start, end, samples_per_tick; } Loop;
    static const Loop loops[21] = {
        {1537, 7169, 600}, {145, 7057, 637}, {129, 7297, 618},
        {113, 4593, 600}, {1081, 4921, 600}, {1, 4609, 637},
        {1, 120961, 608}, {33, 5665, 600}, {1, 7681, 637},
        {51, 4851, 600}, {1, 5121, 600}, {1, 8961, 637},
        {1, 4481, 637}, {1, 6401, 637}, {1, 7681, 637},
        {321, 7361, 637}, {1, 5761, 637}, {3881, 9001, 637},
        {1, 5121, 637}, {1, 4481, 637}, {1, 6401, 637}
    };
    const Loop* loop = &loops[track % 21];
    if (start_seconds) *start_seconds = loop->start * loop->samples_per_tick / 49716.0f;
    if (end_seconds) *end_seconds = loop->end * loop->samples_per_tick / 49716.0f;
}
