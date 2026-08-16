#include "lp_game.h"

#include <limits.h>
#include <string.h>

static int bit_get(const uint8_t* plane, int x, int y) {
    if (x < 0 || x >= LP_LEVEL_WIDTH || y < 0 || y >= LP_LEVEL_HEIGHT) return 0;
    return !!(plane[y * LP_ROW_BYTES + x / 8] & (0x80 >> (x & 7)));
}

static void bit_set(uint8_t* plane, int x, int y, int value) {
    uint8_t* byte;
    uint8_t mask;
    if (x < 0 || x >= LP_LEVEL_WIDTH || y < 0 || y >= LP_LEVEL_HEIGHT) return;
    byte = &plane[y * LP_ROW_BYTES + x / 8]; mask = 0x80 >> (x & 7);
    if (value) *byte |= mask; else *byte &= (uint8_t)~mask;
}

int lp_game_ground(const LPGame* game, int x, int y) { return bit_get(game->level->solid, x, y); }
int lp_game_steel(const LPGame* game, int x, int y) { return bit_get(game->level->steel, x, y); }
void lp_game_set_ground(LPGame* game, int x, int y, int value) {
    if (!value && lp_game_steel(game, x, y)) return;
    bit_set(game->level->solid, x, y, value);
    bit_set(game->level->visual, x, y, value);
    if (game->level->visual_bayer2) bit_set(game->level->visual_bayer2, x, y, value);
    if (game->level->visual_cluster2) bit_set(game->level->visual_cluster2, x, y, value);
}

static void state(LPLemming* lemming, LPLemmingState value) {
    lemming->state = (uint8_t)value; lemming->frame = 0; lemming->state_counter = 0;
}

static void start_fall(LPLemming* lemming) {
    state(lemming, LP_STATE_FALL);
    /* Fall distance counts actual source pixels, so a fresh fall starts at
       zero. DOS seeds three because its first falling frame also moves three
       pixels; this port's slower step must not inherit that constant, or every
       transition credits three pixels the lemming never fell and the splat
       check fires four pixels early. Never retain the distance from an earlier
       fall through a walking or working state. */
    lemming->fall_distance = 0;
}

static int direction(const LPLemming* lemming) { return lemming->right ? 1 : -1; }

void lp_game_emit_sound(LPGame* game, LPSoundEvent event) {
    unsigned i;
    if (!game || event >= LP_SOUND_COUNT) return;
    for (i = 0; i < game->sound_count; ++i)
        if (game->sound_events[(game->sound_head + i) % LP_SOUND_QUEUE_CAPACITY] == (uint8_t)event)
            return;
    if (game->sound_count >= LP_SOUND_QUEUE_CAPACITY) return;
    game->sound_events[(game->sound_head + game->sound_count) % LP_SOUND_QUEUE_CAPACITY] = (uint8_t)event;
    ++game->sound_count;
}

int lp_game_take_sound(LPGame* game, LPSoundEvent* event) {
    if (!game || !event || !game->sound_count) return 0;
    *event = (LPSoundEvent)game->sound_events[game->sound_head];
    game->sound_head = (uint8_t)((game->sound_head + 1) % LP_SOUND_QUEUE_CAPACITY);
    --game->sound_count;
    return 1;
}

static void clear_rect(LPGame* game, int x1, int y1, int x2, int y2) {
    int x, y;
    for (y = y1; y <= y2; ++y)
        for (x = x1; x <= x2; ++x) lp_game_set_ground(game, x, y, 0);
}

static int one_way_allows(const LPGame* game, int x, int y, int dx) {
    unsigned i;
    if (!dx) return 1;
    for (i = 0; i < game->level->meta.object_count; ++i) {
        const LPObject* object = &game->level->objects[i];
        if ((object->trigger_effect != 7 && object->trigger_effect != 8) ||
            x < object->trigger_x1 || x >= object->trigger_x2 ||
            y < object->trigger_y1 || y >= object->trigger_y2) continue;
        return object->trigger_effect == 7 ? dx < 0 : dx > 0;
    }
    return 1;
}

static void clear_mask(LPGame* game, unsigned offset, int width, int height,
                       int origin_x, int origin_y, int dx) {
    const uint8_t* data = game->level->terrain_masks;
    int x, y, row_bytes = (width + 7) / 8;
    if (!data || offset + (unsigned)(row_bytes * height) > LP_TERRAIN_MASK_BYTES) return;
    data += offset;
    for (y = 0; y < height; ++y) for (x = 0; x < width; ++x)
        if ((data[y * row_bytes + x / 8] & (0x80 >> (x & 7))) &&
            one_way_allows(game, origin_x + x, origin_y + y, dx))
            lp_game_set_ground(game, origin_x + x, origin_y + y, 0);
}

static void explode(LPGame* game, const LPLemming* lemming) {
    int x, y;
    if (game->level->terrain_masks) {
        clear_mask(game, 264, 16, 22, lemming->x - 8, lemming->y - 14, 0); return;
    }
    /* Fallback used by synthetic unit tests that do not load an asset pack. */
    for (y = -12; y <= 10; ++y) for (x = -11; x <= 11; ++x)
        if (x * x * 100 + y * y * 84 <= 12100)
            lp_game_set_ground(game, lemming->x + x, lemming->y + y, 0);
}

static void remove_lemming(LPGame* game, LPLemming* lemming) {
    if (lemming->active) { lemming->active = 0; lemming->state = LP_STATE_REMOVED; if (game->alive) --game->alive; }
}

static void spawn(LPGame* game) {
    unsigned index, entrance;
    const LPObject* object;
    if (game->released >= game->level->meta.lemming_count || !game->entrance_count) return;
    for (index = 0; index < LP_MAX_LEMMINGS && game->lemmings[index].active; ++index) {}
    if (index == LP_MAX_LEMMINGS) return;
    entrance = game->released % game->entrance_count;
    object = &game->level->objects[game->entrance_objects[entrance]];
    memset(&game->lemmings[index], 0, sizeof game->lemmings[index]);
    game->lemmings[index].x = object->x + 24;
    game->lemmings[index].y = object->y + 14;
    game->lemmings[index].right = 1;
    game->lemmings[index].active = 1;
    start_fall(&game->lemmings[index]);
    ++game->released; ++game->alive;
}

void lp_game_init(LPGame* game, LPLevelAssets* level) {
    unsigned i;
    memset(game, 0, sizeof *game); game->level = level;
    game->release_rate = level->meta.release_rate;
    /* The hatch releases its first lemming a fixed two seconds after the
       level starts; only the gap between later lemmings follows the release
       rate. Seeding the counter so the first spawn lands at that fixed delay
       keeps slow levels from waiting a whole release interval for lemming
       one. At very high rates the interval is shorter than the delay, so the
       first spawn simply arrives with the interval. */
    {
        unsigned interval = LP_RELEASE_INTERVAL(level->meta.release_rate);
        game->release_counter = (uint16_t)(interval > LP_FIRST_RELEASE_TICKS
                                           ? interval - LP_FIRST_RELEASE_TICKS : 0);
    }
    game->remaining_ticks = (uint16_t)(level->meta.time_minutes * 60u * LP_TICKS_PER_SECOND);
    for (i = 0; i < level->meta.object_count && game->entrance_count < LP_MAX_ENTRANCES; ++i)
        if (level->objects[i].object_id == 1) game->entrance_objects[game->entrance_count++] = (uint8_t)i;
}

static int trigger(LPGame* game, LPLemming* lemming) {
    unsigned i;
    for (i = 0; i < game->level->meta.object_count; ++i) {
        const LPObject* object = &game->level->objects[i];
        if (!object->trigger_effect || lemming->x < object->trigger_x1 || lemming->x >= object->trigger_x2 ||
            lemming->y < object->trigger_y1 || lemming->y >= object->trigger_y2) continue;
        if (object->trigger_effect == 1) {
            state(lemming, LP_STATE_EXIT); lemming->disabled = 1;
            lp_game_emit_sound(game, LP_SOUND_YIPPEE); return 1;
        }
        if (object->trigger_effect == 5) {
            state(lemming, LP_STATE_DROWN); lemming->disabled = 1;
            lp_game_emit_sound(game, LP_SOUND_DROWN); return 1;
        }
        if (object->trigger_effect == 4 || object->trigger_effect == 6) {
            LPSoundEvent sound = LP_SOUND_TRAP;
            if (game->object_frames[i]) continue;
            game->object_frames[i] = 1;
            if (object->trigger_effect == 6 || object->sound_effect == 7) sound = LP_SOUND_BURN;
            else if (object->sound_effect == 6) sound = LP_SOUND_ELECTRIC;
            else if (object->sound_effect == 9) sound = LP_SOUND_CHAIN;
            else if (object->sound_effect == 15) sound = LP_SOUND_THUD;
            lp_game_emit_sound(game, sound);
            state(lemming, LP_STATE_BURN); lemming->disabled = 1; return 1;
        }
    }
    return 0;
}

static void fall(LPGame* game, LPLemming* lemming) {
    int distance, limit;
    if (lemming->floater && lemming->fall_distance > 16) { state(lemming, LP_STATE_FLOAT); return; }
    /* Playdate's 2x view makes the original three-pixel step look excessive,
       so falling runs slightly under it on the fixed-point phase from
       lp_game.h. */
    lemming->state_counter = (uint8_t)(lemming->state_counter + LP_FALL_EXTRA);
    if (lemming->state_counter >= LP_FALL_PHASE) {
        lemming->state_counter = (uint8_t)(lemming->state_counter - LP_FALL_PHASE);
        limit = LP_FALL_STEP + 1;
    } else limit = LP_FALL_STEP;
    for (distance = 0; distance < limit &&
         !lp_game_ground(game, lemming->x, lemming->y + distance); ++distance) {}
    lemming->y += distance;
    /* Count every pixel travelled, including the partial step that lands, so
       the fatal distance stays a real distance at any falling speed. */
    lemming->fall_distance = (uint8_t)(lemming->fall_distance + distance);
    if (distance < limit) {
        if (lemming->fall_distance > LP_FATAL_FALL_PIXELS) {
            state(lemming, LP_STATE_SPLAT); lp_game_emit_sound(game, LP_SOUND_SPLAT);
        } else state(lemming, LP_STATE_WALK);
    }
}

static void float_down(LPGame* game, LPLemming* lemming) {
    static const int8_t speed[16] = {3, 3, 3, 3, -1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2};
    int amount = speed[lemming->frame < 16 ? lemming->frame : 15], i;
    for (i = 0; i < amount; ++i) {
        if (lp_game_ground(game, lemming->x, lemming->y + i)) {
            lemming->y += i; state(lemming, LP_STATE_WALK); return;
        }
    }
    lemming->y += amount;
    if (lemming->frame >= 16) lemming->frame = 8;
}

static void walk(LPGame* game, LPLemming* lemming) {
    int up, down, dx = direction(lemming);
    unsigned i;
    /* Blockers turn walkers around before their bodies overlap. */
    for (i = 0; i < LP_MAX_LEMMINGS; ++i) {
        const LPLemming* blocker = &game->lemmings[i];
        if (blocker == lemming || !blocker->active || blocker->state != LP_STATE_BLOCK) continue;
        if (lemming->y >= blocker->y - 10 && lemming->y <= blocker->y + 4 &&
            ((dx > 0 && lemming->x >= blocker->x - 7 && lemming->x < blocker->x) ||
             (dx < 0 && lemming->x <= blocker->x + 7 && lemming->x > blocker->x))) {
            lemming->right = !lemming->right; return;
        }
    }
    lemming->x += dx;
    for (up = 0; up < 8 && lp_game_ground(game, lemming->x, lemming->y - up); ++up) {}
    if (up == 8) {
        if (lemming->climber) state(lemming, LP_STATE_CLIMB);
        else lemming->right = !lemming->right;
    } else if (up) {
        lemming->y -= up - 1;
        if (up > 3) state(lemming, LP_STATE_JUMP);
    } else {
        for (down = 1; down < 4 && !lp_game_ground(game, lemming->x, lemming->y + down); ++down) {}
        lemming->y += down;
        if (down == 4) start_fall(lemming);
    }
}

static void climb(LPGame* game, LPLemming* lemming) {
    int dx = direction(lemming);
    if (lemming->frame < 4) {
        if (!lp_game_ground(game, lemming->x, lemming->y - (int)lemming->frame - 7)) {
            lemming->y = lemming->y - (int)lemming->frame + 2;
            state(lemming, LP_STATE_HOIST);
        }
    } else {
        --lemming->y;
        if (lp_game_ground(game, lemming->x - dx, lemming->y - 8)) {
            lemming->right = !lemming->right; lemming->x -= 2 * dx;
            start_fall(lemming);
        }
    }
    lemming->frame &= 7;
}

static void build(LPGame* game, LPLemming* lemming) {
    int dx = direction(lemming), x, i;
    lemming->frame &= 15;
    if (lemming->frame == 9) {
        x = lemming->x + (dx > 0 ? 0 : -4);
        for (i = 0; i < 6; ++i) lp_game_set_ground(game, x + i, lemming->y - 1, 1);
        if (lemming->bricks_left >= 9) lp_game_emit_sound(game, LP_SOUND_BUILDER_WARNING);
    } else if (lemming->frame == 0) {
        --lemming->y;
        for (i = 0; i < 2; ++i) {
            lemming->x += dx;
            if (lp_game_ground(game, lemming->x, lemming->y - 1)) {
                lemming->right = !lemming->right; state(lemming, LP_STATE_WALK); return;
            }
        }
        if (++lemming->bricks_left >= 12) { state(lemming, LP_STATE_SHRUG); return; }
        if (lp_game_ground(game, lemming->x + 2 * dx, lemming->y - 9)) {
            lemming->right = !lemming->right; state(lemming, LP_STATE_WALK);
        }
    }
}

static void bash(LPGame* game, LPLemming* lemming) {
    int phase = lemming->frame & 15, dx = direction(lemming), x, y, found = 0;
    if (phase >= 2 && phase <= 5) {
        if (game->level->terrain_masks) {
            unsigned base = dx > 0 ? 0 : 80;
            clear_mask(game, base + (unsigned)(phase - 2) * 20, 16, 10,
                       lemming->x - 8, lemming->y - 10, dx);
        } else {
            int reach = 2 + (phase - 2) * 2;
            x = lemming->x + dx * reach;
            clear_rect(game, dx > 0 ? x - 1 : x, lemming->y - 9,
                       dx > 0 ? x : x + 1, lemming->y + 1);
        }
    }
    if (phase == 5) {
        for (x = 1; x <= 4; ++x) for (y = -8; y <= -3; ++y)
            if (lp_game_ground(game, lemming->x + dx * (7 + x), lemming->y + y)) found = 1;
        if (!found) { state(lemming, LP_STATE_WALK); return; }
    }
    if (phase > 10) {
        int down;
        lemming->x += dx;
        for (down = 0; down < 3 && !lp_game_ground(game, lemming->x, lemming->y + down); ++down) {}
        lemming->y += down;
        if (down == 3) start_fall(lemming);
    }
}

static void mine(LPGame* game, LPLemming* lemming) {
    int phase = lemming->frame % 24, dx = direction(lemming), x, y;
    if (phase == 1 || phase == 2) {
        if (game->level->terrain_masks) {
            unsigned base = dx > 0 ? 160 : 212;
            clear_mask(game, base + (unsigned)(phase - 1) * 26, 16, 13,
                       lemming->x - 8, lemming->y - 12, dx);
        } else {
            int reach = phase == 1 ? 2 : 5;
            for (y = -5 + phase; y <= 2 + phase; ++y)
                for (x = 0; x <= 7; ++x)
                    if (x + (y + 2) >= reach)
                        lp_game_set_ground(game, lemming->x + dx * x, lemming->y + y, 0);
        }
    }
    if (phase == 3) { ++lemming->y; lemming->x += dx; }
    else if (phase == 15) lemming->x += dx;
    if ((phase == 3 || phase == 15) && !lp_game_ground(game, lemming->x, lemming->y))
        start_fall(lemming);
}

static void dig(LPGame* game, LPLemming* lemming) {
    int x, removed = 0;
    if (lemming->state_counter == 0) {
        clear_rect(game, lemming->x - 4, lemming->y - 2, lemming->x + 4, lemming->y - 1);
        lemming->state_counter = 1;
    }
    if (!(lemming->frame & 7)) {
        ++lemming->y;
        for (x = lemming->x - 4; x <= lemming->x + 4; ++x) {
            if (lp_game_ground(game, x, lemming->y - 1) && !lp_game_steel(game, x, lemming->y - 1)) removed = 1;
            lp_game_set_ground(game, x, lemming->y - 1, 0);
        }
        if (!removed) start_fall(lemming);
    }
}

static void process(LPGame* game, LPLemming* lemming) {
    if (lemming->x < 0 || lemming->x >= LP_LEVEL_WIDTH || lemming->y < -5 || lemming->y >= LP_LEVEL_HEIGHT + 6) {
        remove_lemming(game, lemming); return;
    }
    ++lemming->frame;
    if (lemming->countdown && --lemming->countdown == 0) {
        /* DOS skips the Oh-no pose when a falling/floating fuse expires. */
        if (lemming->state == LP_STATE_FALL || lemming->state == LP_STATE_FLOAT)
            state(lemming, LP_STATE_EXPLODE);
        else {
            state(lemming, LP_STATE_OHNO);
            lp_game_emit_sound(game, LP_SOUND_OH_NO);
        }
        lemming->disabled = 1;
    }
    switch ((LPLemmingState)lemming->state) {
        case LP_STATE_FALL: fall(game, lemming); break;
        case LP_STATE_WALK: walk(game, lemming); break;
        case LP_STATE_JUMP:
            if (!lp_game_ground(game, lemming->x, lemming->y - 1)) state(lemming, LP_STATE_WALK);
            else --lemming->y;
            break;
        case LP_STATE_CLIMB: climb(game, lemming); break;
        case LP_STATE_HOIST:
            if (lemming->frame <= 4) lemming->y -= 2;
            if (lemming->frame >= 8) state(lemming, LP_STATE_WALK);
            break;
        case LP_STATE_FLOAT: float_down(game, lemming); break;
        case LP_STATE_BLOCK:
            if (!lp_game_ground(game, lemming->x, lemming->y)) start_fall(lemming);
            break;
        case LP_STATE_BUILD: build(game, lemming); break;
        case LP_STATE_SHRUG: if (lemming->frame >= 8) state(lemming, LP_STATE_WALK); break;
        case LP_STATE_BASH: bash(game, lemming); break;
        case LP_STATE_MINE: mine(game, lemming); break;
        case LP_STATE_DIG: dig(game, lemming); break;
        case LP_STATE_OHNO:
            if (!lp_game_ground(game, lemming->x, lemming->y)) ++lemming->y;
            if (lemming->frame >= 16) state(lemming, LP_STATE_EXPLODE);
            break;
        case LP_STATE_EXPLODE:
            lemming->disabled = 1;
            if (lemming->frame == 1) {
                explode(game, lemming); lp_game_emit_sound(game, LP_SOUND_EXPLODE);
            }
            if (lemming->frame >= 52) remove_lemming(game, lemming);
            break;
        case LP_STATE_SPLAT: case LP_STATE_DROWN: case LP_STATE_BURN:
            lemming->disabled = 1; if (lemming->frame >= 16) remove_lemming(game, lemming); break;
        case LP_STATE_EXIT:
            if (lemming->frame >= 8) { ++game->rescued; remove_lemming(game, lemming); } break;
        default: break;
    }
    if (lemming->active && !lemming->disabled) trigger(game, lemming);
}

void lp_game_tick(LPGame* game) {
    unsigned i;
    if (!game || game->paused) return;
    ++game->tick;
    for (i = 0; i < game->level->meta.object_count; ++i) {
        const LPObject* object = &game->level->objects[i];
        if (game->object_frames[i] && ++game->object_frames[i] >= object->frame_count)
            game->object_frames[i] = 0;
    }
    if (++game->release_counter >= LP_RELEASE_INTERVAL(game->release_rate)) { game->release_counter = 0; spawn(game); }
    /* The visible level clock begins with the playable release phase, not
       while the entrance hatch is opening. */
    if (game->released && game->remaining_ticks) --game->remaining_ticks;
    for (i = 0; i < LP_MAX_LEMMINGS; ++i) if (game->lemmings[i].active) process(game, &game->lemmings[i]);
    /* DOS nuke order is slot order, one slot per update. Every newly armed
       lemming receives the complete five-second fuse. */
    if (game->nuking) while (game->nuke_index < LP_MAX_LEMMINGS) {
        LPLemming* lemming = &game->lemmings[game->nuke_index++];
        if (!lemming->active) continue;
        if (!lemming->countdown && !lemming->disabled)
            lemming->countdown = LP_BOMBER_FUSE_TICKS;
        break;
    }
}

int lp_game_select_at(const LPGame* game, int x, int y) {
    int best = -1, best_distance = INT_MAX;
    unsigned i;
    for (i = 0; i < LP_MAX_LEMMINGS; ++i) {
        const LPLemming* lemming = &game->lemmings[i];
        int dx, dy, distance;
        if (!lemming->active || lemming->disabled) continue;
        dx = x - lemming->x;
        dy = y - (lemming->y + LP_SELECT_CENTER_Y_OFFSET);
        if (dx < -LP_SELECT_RADIUS_X || dx > LP_SELECT_RADIUS_X ||
            dy < -LP_SELECT_RADIUS_Y || dy > LP_SELECT_RADIUS_Y) continue;
        distance = dx * dx + dy * dy;
        if (distance < best_distance) { best = (int)i; best_distance = distance; }
    }
    return best;
}

LPHoverInfo lp_game_hover_info(const LPGame* game, int x, int y) {
    LPHoverInfo result = {-1, 0, LP_STATE_NONE};
    int best_distance = INT_MAX;
    unsigned i;
    if (!game) return result;
    for (i = 0; i < LP_MAX_LEMMINGS; ++i) {
        const LPLemming* lemming = &game->lemmings[i];
        int dx, dy, distance;
        if (!lemming->active || lemming->disabled) continue;
        dx = x - lemming->x;
        dy = y - (lemming->y + LP_SELECT_CENTER_Y_OFFSET);
        if (dx < -LP_SELECT_RADIUS_X || dx > LP_SELECT_RADIUS_X ||
            dy < -LP_SELECT_RADIUS_Y || dy > LP_SELECT_RADIUS_Y) continue;
        if (result.count < 255) ++result.count;
        distance = dx * dx + dy * dy;
        if (distance < best_distance) {
            best_distance = distance; result.lemming_index = (int)i;
            result.state = (LPLemmingState)lemming->state;
        }
    }
    return result;
}

int lp_game_can_assign(const LPGame* game, unsigned index, LPSkill skill) {
    const LPLemming* lemming;
    if (!game || !game->level || index >= LP_MAX_LEMMINGS || skill > LP_SKILL_DIGGER ||
        !game->level->meta.skills[skill]) return 0;
    lemming = &game->lemmings[index];
    if (!lemming->active || lemming->disabled) return 0;
    if (skill == LP_SKILL_CLIMBER) return !lemming->climber;
    if (skill == LP_SKILL_FLOATER) return !lemming->floater;
    if (skill == LP_SKILL_BOMBER) return !lemming->countdown;
    if (lemming->state != LP_STATE_WALK) return 0;
    if (skill == LP_SKILL_DIGGER && !lp_game_ground(game, lemming->x, lemming->y)) return 0;
    return 1;
}

int lp_game_assign(LPGame* game, unsigned index, LPSkill skill) {
    static const LPLemmingState action_for_skill[8] = {
        LP_STATE_NONE, LP_STATE_NONE, LP_STATE_NONE, LP_STATE_BLOCK,
        LP_STATE_BUILD, LP_STATE_BASH, LP_STATE_MINE, LP_STATE_DIG
    };
    LPLemming* lemming;
    if (!lp_game_can_assign(game, index, skill)) return 0;
    lemming = &game->lemmings[index];
    if (skill == LP_SKILL_CLIMBER) lemming->climber = 1;
    else if (skill == LP_SKILL_FLOATER) lemming->floater = 1;
    else if (skill == LP_SKILL_BOMBER) lemming->countdown = LP_BOMBER_FUSE_TICKS;
    else {
        state(lemming, action_for_skill[skill]);
        if (skill == LP_SKILL_BUILDER) lemming->bricks_left = 0;
    }
    --game->level->meta.skills[skill];
    lp_game_emit_sound(game, LP_SOUND_ASSIGN);
    return 1;
}

int lp_game_finished(const LPGame* game) {
    return game && ((!game->remaining_ticks) ||
           (game->released >= game->level->meta.lemming_count && game->alive == 0));
}

int lp_game_won(const LPGame* game) {
    return game && lp_game_finished(game) && game->rescued >= game->level->meta.rescue_count;
}

void lp_game_nuke(LPGame* game) {
    if (!game || game->nuking) return;
    game->nuking = 1;
    game->nuke_index = 0;
    game->released = game->level->meta.lemming_count;
}

void lp_game_adjust_release_rate(LPGame* game, int delta) {
    int value;
    if (!game || !game->level) return;
    value = (int)game->release_rate + delta;
    if (value < game->level->meta.release_rate) value = game->level->meta.release_rate;
    if (value > 99) value = 99;
    game->release_rate = (uint16_t)value;
}

unsigned lp_game_object_frame(const LPGame* game, unsigned object_index) {
    const LPObject* object;
    unsigned frame;
    if (!game || object_index >= game->level->meta.object_count) return 0;
    object = &game->level->objects[object_index];
    if (!object->frame_count) return 0;
    if (!(object->anim_flags & 1))
        return (game->tick + object->start_frame) % object->frame_count;
    if (object->trigger_effect == 4 || object->trigger_effect == 6)
        return game->object_frames[object_index] % object->frame_count;
    frame = game->tick + object->start_frame;
    return frame < object->frame_count ? frame : 0;
}

unsigned lp_game_sprite_frame(const LPLemming* lemming, unsigned frame_count) {
    unsigned frame;
    if (!lemming || !frame_count) return 0;
    frame = lemming->frame;
    /* Floaters have sixteen logical animation ticks but eight source images.
       The first sixteen ticks open the umbrella at two ticks per image. After
       that, float_down loops logical ticks 8..15, hence source images 4..7. */
    if (lemming->state == LP_STATE_FLOAT) frame /= 2u;
    /* Falling animation is paced against the falling speed, so the cycle keeps
       the same relationship to travelled distance when that speed changes. */
    else if (lemming->state == LP_STATE_FALL)
        frame = frame * (LP_FALL_STEP * LP_FALL_PHASE + LP_FALL_EXTRA) / (3u * LP_FALL_PHASE);
    return frame % frame_count;
}
