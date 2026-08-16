#ifndef LP_MUSIC_H
#define LP_MUSIC_H

/* Return the ADLIB.DAT song index for a canonical level index (0..119). */
unsigned lp_music_for_level(unsigned level_index);

/* Return exact PCM loop boundaries generated from the DOS command stream. */
void lp_music_loop_range(unsigned track, float* start_seconds, float* end_seconds);

#endif
