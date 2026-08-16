/*
 * lp_game: the portable simulation.
 *
 * This is the whole game rule set: lemming states, the eight skills, terrain
 * destruction, object triggers, release timing, and win and loss conditions.
 * It contains no platform code and calls no SDK, which is what lets the host
 * build and the unit tests run the identical simulation the device runs.
 *
 * The platform adapter owns input, drawing, audio, and storage. It advances
 * the world with lp_game_tick, reads state out of LPGame to draw it, and
 * drains queued sound events. Terrain lives in the caller's LPLevelAssets and
 * is mutated in place as skills cut through it.
 *
 * Timing is in ticks, LP_TICKS_PER_SECOND of them per second, matching the
 * rate the DOS game ran its logic at. Distances are in source pixels, not
 * screen pixels, so the presentation scale never changes the rules.
 */
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

/* The box around a lemming that a cursor selects it within, in source pixels,
   offset upward because the anchor point is at the feet. */
#define LP_SELECT_RADIUS_X 5
#define LP_SELECT_RADIUS_Y 6
#define LP_SELECT_CENTER_Y_OFFSET (-5)

/* Gameplay reports sound as events rather than playing anything, so the
   adapter decides how they map to samples. Duplicates within one tick
   collapse, so a queue this size cannot overflow in practice. */
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

/* What sits under the cursor: the lemming that would be selected, how many
   overlap there, and what it is doing, so a HUD can describe the target. */
typedef struct {
    int lemming_index;
    uint8_t count;
    LPLemmingState state;
} LPHoverInfo;

/* One lemming. `x` and `y` are source pixels, with `y` at the feet.
 *
 * `frame` counts ticks in the current state and drives animation.
 * `state_counter` is per-state scratch: the fixed-point phase while falling,
 * a one-shot flag while digging. `right` is the facing. `climber` and
 * `floater` are permanent once assigned, unlike the other skills, which are
 * states. `disabled` marks a lemming that can no longer be assigned to,
 * because it is dying or leaving. `countdown` is the bomber fuse in ticks.
 * `fall_distance` accumulates real pixels fallen and decides the splat.
 */
typedef struct {
    int16_t x, y;
    uint8_t state, frame, state_counter, active;
    uint8_t right, climber, floater, disabled;
    uint8_t countdown, fall_distance, bricks_left, reserved;
} LPLemming;

/* The whole mutable world. Copyable and comparable, holding no pointers other
 * than the level it was initialised with, which is what makes deterministic
 * host-side simulation straightforward.
 *
 * `released` counts lemmings the hatch has produced, `alive` those still in
 * play, and `rescued` those that reached the exit. `remaining_ticks` is the
 * level clock, which only starts once the first lemming is out.
 */
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

/* Start a level. `level` must outlive the game and is mutated as terrain is
   destroyed, so reload it before replaying. */
void lp_game_init(LPGame* game, LPLevelAssets* level);

/* Advance one tick: release, object animation, the clock, every lemming, and
   the nuke. Does nothing while paused. */
void lp_game_tick(LPGame* game);

/* Index of the lemming a cursor at these source coordinates would select, or
   -1. Prefers the one whose assignment matters most when several overlap. */
int lp_game_select_at(const LPGame* game, int x, int y);

/* Whether a skill can be applied: it must be in stock and the lemming must be
   in a state that accepts it. Use this to grey out an unavailable action
   rather than discovering it by a failed assign. */
int lp_game_can_assign(const LPGame* game, unsigned lemming_index, LPSkill skill);

/* Apply a skill, spending one from the level's stock. Nonzero on success. */
int lp_game_assign(LPGame* game, unsigned lemming_index, LPSkill skill);

/* Same targeting as lp_game_select_at, with the extra detail a HUD wants. */
LPHoverInfo lp_game_hover_info(const LPGame* game, int x, int y);

/* Queue a sound event. Repeats within a tick collapse into one. */
void lp_game_emit_sound(LPGame* game, LPSoundEvent event);

/* Take the oldest queued sound event. Zero when the queue is empty, so the
   adapter drains it in a loop each frame. */
int lp_game_take_sound(LPGame* game, LPSoundEvent* event);

/* Terrain queries and edits in source pixels. lp_game_set_ground updates the
   collision and every visual plane together, and refuses to clear steel, so
   callers cannot accidentally destroy indestructible terrain. */
int lp_game_ground(const LPGame* game, int x, int y);
int lp_game_steel(const LPGame* game, int x, int y);
void lp_game_set_ground(LPGame* game, int x, int y, int value);

/* Whether the level has ended, and whether enough lemmings were rescued. */
int lp_game_finished(const LPGame* game);
int lp_game_won(const LPGame* game);

/* Arm every lemming, one per tick in slot order, each with a full fuse. */
void lp_game_nuke(LPGame* game);

/* Change the release rate, clamped to the level's authored minimum and 99. */
void lp_game_adjust_release_rate(LPGame* game, int delta);

/* Current animation frame of a placed object, for drawing traps and water. */
unsigned lp_game_object_frame(const LPGame* game, unsigned object_index);
unsigned lp_game_sprite_frame(const LPLemming* lemming, unsigned frame_count);

#endif
