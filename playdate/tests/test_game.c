#include "lp_game.h"
#include "lp_music.h"
#include "../src/hud_font.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void set(uint8_t* p, int x, int y) { p[y * LP_ROW_BYTES + x / 8] |= 0x80 >> (x & 7); }

static void walking(LPGame* game, LPLevelAssets* level, int x, int y) {
    memset(game, 0, sizeof *game); game->level = level; game->alive = 1;
    game->lemmings[0].active = 1; game->lemmings[0].right = 1;
    game->lemmings[0].state = LP_STATE_WALK; game->lemmings[0].x = x; game->lemmings[0].y = y;
}

static int take_sound(LPGame* game, LPSoundEvent expected) {
    LPSoundEvent sound;
    while (lp_game_take_sound(game, &sound)) if (sound == expected) return 1;
    return 0;
}

int main(void) {
    uint8_t visual[LP_PLANE_BYTES] = {0}, bayer2[LP_PLANE_BYTES] = {0};
    uint8_t cluster2[LP_PLANE_BYTES] = {0}, solid[LP_PLANE_BYTES] = {0};
    uint8_t steel[LP_PLANE_BYTES] = {0};
    LPLevelAssets level = {0};
    LPGame game;
    int x, fallen;
    float loop_start, loop_end;
    LPSoundEvent sound;

    memset(&game, 0, sizeof game);
    assert(hud_glyph_for('0') == hud_digit_glyphs[0]);
    assert(hud_glyph_for('9') == hud_digit_glyphs[9]);
    assert(hud_glyph_for('A') == hud_letter_glyphs[0]);
    assert(hud_glyph_for('Z') == hud_letter_glyphs[25]);
    assert(hud_glyph_for('O') == hud_letter_glyphs['O' - 'A']);
    assert(memcmp(hud_glyph_for('O'), hud_glyph_for('0'), HUD_GLYPH_HEIGHT) != 0);
    assert(hud_glyph_for(' ') == NULL);
    for (x = 0; x < LP_SOUND_COUNT; ++x)
        lp_game_emit_sound(&game, (LPSoundEvent)x);
    assert(game.sound_count == LP_SOUND_QUEUE_CAPACITY);
    lp_game_emit_sound(&game, LP_SOUND_DOOR); /* Duplicate events coalesce. */
    assert(game.sound_count == LP_SOUND_QUEUE_CAPACITY);
    for (x = 0; x < LP_SOUND_QUEUE_CAPACITY; ++x) {
        assert(lp_game_take_sound(&game, &sound));
        assert(sound == (LPSoundEvent)x);
    }
    assert(!lp_game_take_sound(&game, &sound));
    lp_game_emit_sound(&game, LP_SOUND_FAILURE); /* Queue wraps after draining. */
    assert(lp_game_take_sound(&game, &sound) && sound == LP_SOUND_FAILURE);

    assert(lp_music_for_level(0) == 5);   /* Just dig!: Lemming 1 */
    assert(lp_music_for_level(1) == 6);   /* Lemming 2 */
    assert(lp_music_for_level(16) == 20); /* Tim 10 */
    assert(lp_music_for_level(17) == 5);  /* rotation wraps */
    assert(lp_music_for_level(21) == 1);  /* Beast I */
    assert(lp_music_for_level(43) == 8);  /* Menace */
    assert(lp_music_for_level(74) == 0);  /* Awesome */
    assert(lp_music_for_level(111) == 2); /* Beast II */
    lp_music_loop_range(5, &loop_start, &loop_end);
    assert(loop_start > 0.012f && loop_start < 0.013f);
    assert(loop_end > 59.05f && loop_end < 59.06f);
    level.visual = visual; level.visual_bayer2 = bayer2;
    level.visual_cluster2 = cluster2;
    level.solid = solid; level.steel = steel;
    level.meta.lemming_count = 1; level.meta.release_rate = 99; level.meta.time_minutes = 5;
    level.meta.skills[LP_SKILL_CLIMBER] = 1; level.meta.object_count = 1;
    level.objects[0].object_id = 1; level.objects[0].x = 10; level.objects[0].y = 20;
    for (x = 0; x < 300; ++x) { set(visual, x, 60); set(solid, x, 60); }
    lp_game_init(&game, &level);
    {
        uint16_t initial_time = game.remaining_ticks;
        game.release_rate = 0; game.release_counter = 0;
        lp_game_tick(&game);
        assert(game.released == 0 && game.remaining_ticks == initial_time);
        game.release_rate = 99; game.release_counter = 69;
    }
    level.meta.release_rate = 40; game.release_rate = 50;
    lp_game_adjust_release_rate(&game, -100);
    assert(game.release_rate == level.meta.release_rate);
    lp_game_adjust_release_rate(&game, 1);
    assert(game.release_rate == level.meta.release_rate + 1);
    lp_game_adjust_release_rate(&game, 100);
    assert(game.release_rate == 99);
    level.meta.release_rate = 99; game.release_rate = 99;
    for (x = 0; x < 20; ++x) lp_game_tick(&game);
    assert(game.released == 1 && game.alive == 1);
    assert(game.remaining_ticks == 5 * 60 * LP_TICKS_PER_SECOND - 20);
    assert(game.lemmings[0].state == LP_STATE_WALK);
    assert(lp_game_select_at(&game, game.lemmings[0].x, game.lemmings[0].y - 5) == 0);
    assert(lp_game_hover_info(&game, game.lemmings[0].x,
                              game.lemmings[0].y - 5).count == 1);
    assert(lp_game_select_at(&game, game.lemmings[0].x + LP_SELECT_RADIUS_X,
                             game.lemmings[0].y + LP_SELECT_CENTER_Y_OFFSET +
                             LP_SELECT_RADIUS_Y) == 0);
    assert(lp_game_select_at(&game, game.lemmings[0].x + LP_SELECT_RADIUS_X + 1,
                             game.lemmings[0].y + LP_SELECT_CENTER_Y_OFFSET) == -1);
    assert(lp_game_can_assign(&game, 0, LP_SKILL_CLIMBER));
    assert(lp_game_assign(&game, 0, LP_SKILL_CLIMBER));
    assert(game.lemmings[0].climber);
    assert(!lp_game_can_assign(&game, 0, LP_SKILL_CLIMBER));
    assert(take_sound(&game, LP_SOUND_ASSIGN));

    memset(visual, 0, sizeof visual); memset(solid, 0, sizeof solid);
    for (x = 0; x < 300; ++x) { set(visual, x, 60); set(solid, x, 60); }
    level.meta.skills[LP_SKILL_BUILDER] = 1; walking(&game, &level, 30, 60);
    assert(lp_game_assign(&game, 0, LP_SKILL_BUILDER));
    for (x = 0; x < 9; ++x) lp_game_tick(&game);
    assert(lp_game_ground(&game, 30, 59));
    assert((bayer2[59 * LP_ROW_BYTES + 30 / 8] & (0x80 >> (30 & 7))) != 0);
    assert((cluster2[59 * LP_ROW_BYTES + 30 / 8] & (0x80 >> (30 & 7))) != 0);

    level.meta.skills[LP_SKILL_DIGGER] = 1; walking(&game, &level, 40, 60);
    assert(lp_game_assign(&game, 0, LP_SKILL_DIGGER));
    for (x = 0; x < 8; ++x) lp_game_tick(&game);
    assert(!lp_game_ground(&game, 40, 60));

    level.meta.skills[LP_SKILL_BLOCKER] = 1; walking(&game, &level, 50, 60);
    assert(lp_game_assign(&game, 0, LP_SKILL_BLOCKER));
    assert(game.lemmings[0].state == LP_STATE_BLOCK);

    /* Falling counts real pixels travelled: early ticks take the base step,
       and the fixed-point phase adds one extra pixel LP_FALL_EXTRA times per
       LP_FALL_PHASE ticks. Stated against the constants so tuning the falling
       speed does not silently invalidate the distance bookkeeping. */
    walking(&game, &level, 55, 30); game.lemmings[0].state = LP_STATE_FALL;
    lp_game_tick(&game);
    assert(game.lemmings[0].y == 30 + LP_FALL_STEP);
    assert(game.lemmings[0].fall_distance == LP_FALL_STEP);
    lp_game_tick(&game);
    assert(game.lemmings[0].y == 30 + 2 * LP_FALL_STEP);
    assert(game.lemmings[0].fall_distance == 2 * LP_FALL_STEP);
    walking(&game, &level, 400, 20); game.lemmings[0].state = LP_STATE_FALL;
    for (x = 0; x < (int)LP_FALL_PHASE; ++x) lp_game_tick(&game);
    fallen = LP_FALL_STEP * (int)LP_FALL_PHASE + (int)LP_FALL_EXTRA;
    assert(game.lemmings[0].fall_distance == fallen);
    assert(game.lemmings[0].y == 20 + fallen);

    /* A worker beginning a new fall must not retain the distance from its
       entrance fall, and must not be credited with pixels it never fell:
       fall distance counts actual source pixels, so a fresh fall starts at
       zero. */
    walking(&game, &level, 400, 20);
    game.lemmings[0].state = LP_STATE_MINE;
    game.lemmings[0].frame = 2;
    game.lemmings[0].fall_distance = 61;
    lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_FALL);
    assert(game.lemmings[0].fall_distance == 0);

    /* The fatal boundary is 63 source pixels: a 62-pixel drop through an
       action->fall transition walks away, a 63-pixel drop splats. Seeding the
       transition with a non-zero distance moves this boundary and kills
       lemmings on survivable drops. */
    memset(visual, 0, sizeof visual); memset(solid, 0, sizeof solid);
    for (x = 0; x < 300; ++x) { set(visual, x, 83); set(solid, x, 83); }
    walking(&game, &level, 100, 21);
    game.lemmings[0].state = LP_STATE_MINE; game.lemmings[0].frame = 2;
    for (x = 0; x < 200 && game.lemmings[0].state != LP_STATE_WALK; ++x) {
        lp_game_tick(&game);
        assert(game.lemmings[0].state != LP_STATE_SPLAT);
    }
    assert(game.lemmings[0].state == LP_STATE_WALK && game.lemmings[0].y == 83);

    memset(visual, 0, sizeof visual); memset(solid, 0, sizeof solid);
    for (x = 0; x < 300; ++x) { set(visual, x, 84); set(solid, x, 84); }
    walking(&game, &level, 100, 21);
    game.lemmings[0].state = LP_STATE_MINE; game.lemmings[0].frame = 2;
    for (x = 0; x < 200 && game.lemmings[0].state != LP_STATE_SPLAT; ++x) {
        lp_game_tick(&game);
        assert(game.lemmings[0].state != LP_STATE_WALK);
    }
    assert(game.lemmings[0].state == LP_STATE_SPLAT);
    memset(visual, 0, sizeof visual); memset(solid, 0, sizeof solid);

    level.meta.skills[LP_SKILL_FLOATER] = 1; walking(&game, &level, 55, 30);
    game.lemmings[0].state = LP_STATE_FALL; game.lemmings[0].fall_distance = 20;
    assert(lp_game_assign(&game, 0, LP_SKILL_FLOATER));
    lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_FLOAT);

    memset(visual, 0, sizeof visual); memset(solid, 0, sizeof solid);
    for (x = 0; x < 300; ++x) { set(visual, x, 60); set(solid, x, 60); }
    level.meta.skills[LP_SKILL_BASHER] = 1; walking(&game, &level, 30, 60);
    assert(!lp_game_ground(&game, 31, 56));
    assert(lp_game_can_assign(&game, 0, LP_SKILL_BASHER));
    assert(lp_game_assign(&game, 0, LP_SKILL_BASHER));
    assert(game.lemmings[0].state == LP_STATE_BASH);
    assert(level.meta.skills[LP_SKILL_BASHER] == 0);

    for (x = 50; x <= 60; ++x) {
        int y;
        for (y = 55; y < 80; ++y) { set(visual, x, y); set(solid, x, y); }
    }
    level.meta.skills[LP_SKILL_BASHER] = 1; walking(&game, &level, 54, 60);
    assert(lp_game_assign(&game, 0, LP_SKILL_BASHER));
    for (x = 0; x < 6; ++x) lp_game_tick(&game);
    assert(!lp_game_ground(&game, 56, 55));

    memset(visual, 0, sizeof visual); memset(solid, 0, sizeof solid);
    for (x = 0; x < 300; ++x) { set(visual, x, 60); set(solid, x, 60); }
    level.meta.skills[LP_SKILL_MINER] = 1; walking(&game, &level, 90, 60);
    assert(lp_game_assign(&game, 0, LP_SKILL_MINER));
    for (x = 0; x < 4; ++x) lp_game_tick(&game);
    assert(!lp_game_ground(&game, 90, 60));

    memset(visual, 0, sizeof visual); memset(solid, 0, sizeof solid);
    for (x = 0; x < 300; ++x) { set(visual, x, 60); set(solid, x, 60); }
    level.meta.skills[LP_SKILL_BOMBER] = 1; walking(&game, &level, 70, 60);
    assert(lp_game_assign(&game, 0, LP_SKILL_BOMBER));
    assert(game.lemmings[0].countdown == LP_BOMBER_FUSE_TICKS);
    for (x = 0; x < LP_BOMBER_FUSE_TICKS - 1; ++x) lp_game_tick(&game);
    assert(game.lemmings[0].countdown == 1);
    assert(game.lemmings[0].state == LP_STATE_WALK);
    lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_OHNO);
    assert(take_sound(&game, LP_SOUND_OH_NO));
    for (x = 0; x < 16; ++x) lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_EXPLODE);
    assert(game.lemmings[0].frame == 0);
    lp_game_tick(&game);
    assert(game.lemmings[0].frame == 1);
    assert(!lp_game_ground(&game, game.lemmings[0].x, 60));
    assert(take_sound(&game, LP_SOUND_EXPLODE));

    walking(&game, &level, 70, 40);
    game.lemmings[0].state = LP_STATE_FALL;
    game.lemmings[0].countdown = 1;
    lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_EXPLODE);
    assert(!take_sound(&game, LP_SOUND_OH_NO));

    memset(visual, 0, sizeof visual); memset(solid, 0, sizeof solid);
    for (x = 0; x < 300; ++x) { set(visual, x, 60); set(solid, x, 60); }
    walking(&game, &level, 80, 60); game.lemmings[0].state = LP_STATE_FALL;
    game.lemmings[0].fall_distance = LP_FATAL_FALL_PIXELS; lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_WALK);
    walking(&game, &level, 80, 60); game.lemmings[0].state = LP_STATE_FALL;
    game.lemmings[0].fall_distance = LP_FATAL_FALL_PIXELS + 1; lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_SPLAT);
    assert(take_sound(&game, LP_SOUND_SPLAT));

    memset(steel, 0, sizeof steel); set(steel, 100, 60); set(solid, 100, 60);
    lp_game_set_ground(&game, 100, 60, 0);
    assert(lp_game_ground(&game, 100, 60));

    memset(level.objects, 0, sizeof level.objects); level.meta.object_count = 1;
    level.objects[0].trigger_effect = 1;
    level.objects[0].trigger_x1 = 0; level.objects[0].trigger_x2 = 200;
    level.objects[0].trigger_y1 = 0; level.objects[0].trigger_y2 = 100;
    walking(&game, &level, 120, 60); lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_EXIT);
    assert(take_sound(&game, LP_SOUND_YIPPEE));
    for (x = 0; x < 8; ++x) lp_game_tick(&game);
    assert(game.rescued == 1 && game.alive == 0);

    level.objects[0].trigger_effect = 5;
    walking(&game, &level, 120, 60); lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_DROWN);
    assert(take_sound(&game, LP_SOUND_DROWN));

    level.objects[0].trigger_effect = 4; level.objects[0].sound_effect = 7;
    level.objects[0].frame_count = 10; level.objects[0].anim_flags = 1;
    walking(&game, &level, 120, 60); lp_game_tick(&game);
    assert(game.lemmings[0].state == LP_STATE_BURN);
    assert(game.object_frames[0] == 1);
    assert(take_sound(&game, LP_SOUND_BURN));
    assert(lp_game_object_frame(&game, 0) == 1);

    memset(level.objects, 0, sizeof level.objects); level.meta.object_count = 0;
    walking(&game, &level, 130, 60);
    game.lemmings[1] = game.lemmings[0]; game.lemmings[1].x = 140; game.alive = 2;
    lp_game_nuke(&game);
    assert(game.nuking && game.lemmings[0].countdown == 0 && game.lemmings[1].countdown == 0);
    lp_game_tick(&game);
    assert(game.lemmings[0].countdown == LP_BOMBER_FUSE_TICKS);
    assert(game.lemmings[1].countdown == 0);
    lp_game_tick(&game);
    assert(game.lemmings[0].countdown == LP_BOMBER_FUSE_TICKS - 1);
    assert(game.lemmings[1].countdown == LP_BOMBER_FUSE_TICKS);

    memset(&game.lemmings[2], 0, sizeof game.lemmings[2]);
    game.lemmings[2].state = LP_STATE_FLOAT;
    game.lemmings[2].frame = 0;
    assert(lp_game_sprite_frame(&game.lemmings[2], 8) == 0);
    game.lemmings[2].frame = 1;
    assert(lp_game_sprite_frame(&game.lemmings[2], 8) == 0);
    game.lemmings[2].frame = 8;
    assert(lp_game_sprite_frame(&game.lemmings[2], 8) == 4);
    game.lemmings[2].frame = 15;
    assert(lp_game_sprite_frame(&game.lemmings[2], 8) == 7);

    game.remaining_ticks = 0; level.meta.rescue_count = 1; game.rescued = 1;
    assert(lp_game_finished(&game) && lp_game_won(&game));
    puts("game core tests passed");
    return 0;
}
