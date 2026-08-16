#ifndef LP_GAME_H
#define LP_GAME_H

#include "lp_pack.h"

#include <stdint.h>

#define LP_MAX_LEMMINGS 100
#define LP_MAX_ENTRANCES 4
#define LP_TICKS_PER_SECOND 17
/* Ticks between released lemmings, and the fixed delay before the first. */
#define LP_RELEASE_INTERVAL(rate) (104u - (unsigned)(rate))
#define LP_FIRST_RELEASE_TICKS (2u * LP_TICKS_PER_SECOND)

/* Falling speed. Every tick moves LP_FALL_STEP pixels, and LP_FALL_EXTRA of
   every LP_FALL_PHASE ticks move one more, averaging 2.2 px/tick against the
   DOS three. Raise LP_FALL_EXTRA to speed falling up; at LP_FALL_PHASE it
   becomes a flat three pixels. The fatal distance below is counted in real
   pixels, so it does not move when the speed changes. */
#define LP_FALL_STEP 2
#define LP_FALL_PHASE 40u
#define LP_FALL_EXTRA 8u
#define LP_FATAL_FALL_PIXELS 61u
#define LP_SELECT_RADIUS_X 5
#define LP_SELECT_RADIUS_Y 6
#define LP_SELECT_CENTER_Y_OFFSET (-5)
#define LP_SOUND_QUEUE_CAPACITY 16
#define LP_BOMBER_SECONDS 5
#define LP_BOMBER_FUSE_TICKS (LP_BOMBER_SECONDS * LP_TICKS_PER_SECOND)

typedef enum {
    LP_STATE_NONE, LP_STATE_WALK, LP_STATE_FALL, LP_STATE_JUMP,
    LP_STATE_CLIMB, LP_STATE_HOIST, LP_STATE_FLOAT, LP_STATE_BLOCK,
    LP_STATE_BUILD, LP_STATE_SHRUG, LP_STATE_BASH, LP_STATE_MINE,
    LP_STATE_DIG, LP_STATE_OHNO, LP_STATE_EXPLODE, LP_STATE_SPLAT,
    LP_STATE_DROWN, LP_STATE_BURN, LP_STATE_EXIT, LP_STATE_REMOVED
} LPLemmingState;

typedef enum {
    LP_SKILL_CLIMBER, LP_SKILL_FLOATER, LP_SKILL_BOMBER, LP_SKILL_BLOCKER,
    LP_SKILL_BUILDER, LP_SKILL_BASHER, LP_SKILL_MINER, LP_SKILL_DIGGER
} LPSkill;

typedef enum {
    LP_SOUND_DOOR,
    LP_SOUND_LETS_GO,
    LP_SOUND_SELECT,
    LP_SOUND_ASSIGN,
    LP_SOUND_BUILDER_WARNING,
    LP_SOUND_YIPPEE,
    LP_SOUND_DROWN,
    LP_SOUND_SPLAT,
    LP_SOUND_BURN,
    LP_SOUND_ELECTRIC,
    LP_SOUND_CHAIN,
    LP_SOUND_TRAP,
    LP_SOUND_OH_NO,
    LP_SOUND_EXPLODE,
    LP_SOUND_THUD,
    LP_SOUND_SUCCESS,
    LP_SOUND_FAILURE,
    LP_SOUND_COUNT
} LPSoundEvent;

typedef struct {
    int lemming_index;
    uint8_t count;
    LPLemmingState state;
} LPHoverInfo;

typedef struct {
    int16_t x, y;
    uint8_t state, frame, state_counter, active;
    uint8_t right, climber, floater, disabled;
    uint8_t countdown, fall_distance, bricks_left, reserved;
} LPLemming;

typedef struct {
    LPLevelAssets* level;
    LPLemming lemmings[LP_MAX_LEMMINGS];
    uint16_t tick, released, alive, rescued;
    uint16_t release_rate, release_counter;
    uint16_t remaining_ticks;
    uint8_t paused, nuking, selected_skill, entrance_count;
    uint8_t nuke_index;
    uint8_t sound_head, sound_count;
    uint8_t sound_events[LP_SOUND_QUEUE_CAPACITY];
    uint8_t entrance_objects[LP_MAX_ENTRANCES];
    uint8_t object_frames[LP_MAX_OBJECTS];
} LPGame;

void lp_game_init(LPGame* game, LPLevelAssets* level);
void lp_game_tick(LPGame* game);
int lp_game_select_at(const LPGame* game, int x, int y);
int lp_game_can_assign(const LPGame* game, unsigned lemming_index, LPSkill skill);
int lp_game_assign(LPGame* game, unsigned lemming_index, LPSkill skill);
LPHoverInfo lp_game_hover_info(const LPGame* game, int x, int y);
void lp_game_emit_sound(LPGame* game, LPSoundEvent event);
int lp_game_take_sound(LPGame* game, LPSoundEvent* event);
int lp_game_ground(const LPGame* game, int x, int y);
int lp_game_steel(const LPGame* game, int x, int y);
void lp_game_set_ground(LPGame* game, int x, int y, int value);
int lp_game_finished(const LPGame* game);
int lp_game_won(const LPGame* game);
void lp_game_nuke(LPGame* game);
void lp_game_adjust_release_rate(LPGame* game, int delta);
unsigned lp_game_object_frame(const LPGame* game, unsigned object_index);
unsigned lp_game_sprite_frame(const LPLemming* lemming, unsigned frame_count);

#endif
