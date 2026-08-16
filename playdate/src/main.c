#include "pd_api.h"

#include "lp_adlib.h"
#include "lp_game.h"
#include "lp_music.h"
#include "lp_progress.h"
#include "lp_pack.h"
#include "lynx_action_art.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    SCREEN_LOGO, SCREEN_CREDITS, SCREEN_LEVEL_SELECT,
    SCREEN_GAME, SCREEN_ACTIONS, SCREEN_RESULT
} Screen;

#define HUD_HEIGHT 18

/* Test builds expose every level so any of them can be reached on device
   without playing up to it. Such a build must never touch the player's save
   and must never be published: the progression is read for display only,
   completions are not recorded, and save.dat is never written. The build also
   stages a TEST_UNLOCK marker into the PDX, which package_itch_release.py
   refuses to package. Defined through TEST_UNLOCK=1, never by make release. */
#ifdef LP_TEST_UNLOCK
#define LP_TEST_BUILD 1
#else
#define LP_TEST_BUILD 0
#endif
#define CONTENT_PADDING 24
#define EFFECT_VOICES 4
#define RELEASE_REPEAT_DELAY 0.35f
#define RELEASE_REPEAT_INTERVAL 0.075f

static const char* ground_options[] = {
    "Solid", "Bayer 2", "Cluster 2", "Bayer 4"
};

typedef struct {
    char magic[4];
    uint8_t completed[LP_COMPLETION_BYTES];
    uint8_t last_level;
    uint8_t reserved[12];
} SaveData;

typedef struct {
    PlaydateAPI* pd;
    SDFile* asset_file;
    LPPack pack;
    LPLevelAssets level;
    LPSpriteAtlas sprites;
    LPGame game;
    uint8_t visual[LP_PLANE_BYTES], visual_bayer2[LP_PLANE_BYTES];
    uint8_t visual_cluster2[LP_PLANE_BYTES], solid[LP_PLANE_BYTES], steel[LP_PLANE_BYTES];
    LCDFont* font;
    LCDFont* game_font;
    Screen screen;
    int level_index, cursor_x, cursor_y, camera_x, camera_y;
    int cursor_speed, selected_skill;
    float simulation_time;
    int detail_mode;
    SaveData save;
    uint8_t adlib_image[LP_ADLIB_IMAGE_SIZE];
    LPAdlibPlayer adlib;
    SoundSource* adlib_source;
    AudioSample* effects[LP_SOUND_COUNT];
    SamplePlayer* effect_players[EFFECT_VOICES];
    uint32_t effect_serial[EFFECT_VOICES], next_effect_serial;
    uint8_t effect_priority[EFFECT_VOICES], effect_event[EFFECT_VOICES];
    PDMenuItem* game_menu;
    PDMenuItem* levels_menu;
    PDMenuItem* reset_menu;
    PDMenuItem* ground_menu;
    int ground_mode, action_menu_item, nuke_confirm;
    int release_rate_editing, release_rate_before_edit;
    int release_repeat_direction;
    float release_repeat_time;
    int menu_was_paused;
    unsigned action_animation_tick;
    float action_animation_time;
    int content_left, content_right;
    unsigned music_index;
    uint8_t intro_active, music_started, pending_game_b;
    LCDBitmap* logo;
    char version[16];
    int browser_rating, browser_level;
    float browser_crank;
} App;

static App app;

static void save_progress(void);
static void menu_levels(void* userdata);
static void menu_restart(void* userdata);
static void menu_ground(void* userdata);
static void draw_centered_text(const char* text, int y);

static void set_levels_menu_visible(int visible) {
    if (visible && !app.levels_menu)
        app.levels_menu = app.pd->system->addMenuItem("Choose Level", menu_levels, NULL);
    else if (!visible && app.levels_menu) {
        app.pd->system->removeMenuItem(app.levels_menu);
        app.levels_menu = NULL;
    }
}

static void set_reset_menu_visible(int visible) {
    if (visible && !app.reset_menu)
        app.reset_menu = app.pd->system->addMenuItem("Reset Level", menu_restart, NULL);
    else if (!visible && app.reset_menu) {
        app.pd->system->removeMenuItem(app.reset_menu);
        app.reset_menu = NULL;
    }
}

static void set_dither_menu_visible(int visible) {
    if (visible && !app.ground_menu) {
        app.ground_menu = app.pd->system->addOptionsMenuItem(
            "Dither", ground_options, 4, menu_ground, NULL);
        app.pd->system->setMenuItemValue(app.ground_menu, app.ground_mode);
    } else if (!visible && app.ground_menu) {
        app.pd->system->removeMenuItem(app.ground_menu);
        app.ground_menu = NULL;
    }
}

static int file_read_at(void* context, uint32_t offset, void* output, size_t size) {
    App* state = context;
    return state->pd->file->seek(state->asset_file, (int)offset, SEEK_SET) == 0 &&
           state->pd->file->read(state->asset_file, output, (unsigned)size) == (int)size;
}

static int adlib_audio(void* context, int16_t* left, int16_t* right, int len) {
    (void)right;
    return lp_adlib_read_buffered((LPAdlibPlayer*)context, left, len);
}

static int load_adlib_image(void) {
    SDFile* file = app.pd->file->open("audio/adlib.bin", kFileRead);
    int read;
    if (!file) return 0;
    read = app.pd->file->read(file, app.adlib_image, sizeof app.adlib_image);
    app.pd->file->close(file);
    return read == (int)sizeof app.adlib_image &&
           lp_adlib_init(&app.adlib, app.adlib_image, sizeof app.adlib_image);
}

static int sound_priority(LPSoundEvent event) {
    if (event == LP_SOUND_SELECT || event == LP_SOUND_ASSIGN ||
        event == LP_SOUND_BUILDER_WARNING) return 1;
    if (event == LP_SOUND_LETS_GO || event == LP_SOUND_YIPPEE ||
        event == LP_SOUND_OH_NO || event == LP_SOUND_SPLAT ||
        event == LP_SOUND_DROWN || event == LP_SOUND_BURN ||
        event == LP_SOUND_ELECTRIC || event == LP_SOUND_CHAIN ||
        event == LP_SOUND_TRAP) return 3;
    return 2;
}

/* The DOS captures are mastered at very different levels.  Keep their mixer
 * priority independent from gain and match their perceived loudness to the
 * live OPL music instead of applying one loud SFX master volume. */
static float sound_volume(LPSoundEvent event) {
    switch (event) {
        case LP_SOUND_LETS_GO: return 0.28f;
        case LP_SOUND_OH_NO: return 0.20f;
        case LP_SOUND_YIPPEE: return 0.18f;
        case LP_SOUND_SELECT: return 0.28f;
        case LP_SOUND_ASSIGN: return 0.30f;
        case LP_SOUND_BUILDER_WARNING: return 0.28f;
        case LP_SOUND_THUD: return 0.32f;
        case LP_SOUND_DOOR: return 0.38f;
        case LP_SOUND_EXPLODE: return 0.32f;
        default: return 0.35f;
    }
}

static void play_effect(LPSoundEvent event) {
    int i, slot = -1, priority;
    float volume;
    uint32_t oldest = UINT32_MAX;
    if (event >= LP_SOUND_COUNT || !app.effects[event]) return;
    priority = sound_priority(event);
    for (i = 0; i < EFFECT_VOICES; ++i) {
        if (!app.pd->sound->sampleplayer->isPlaying(app.effect_players[i])) {
            slot = i; break;
        }
        if (app.effect_priority[i] <= priority && app.effect_serial[i] < oldest) {
            oldest = app.effect_serial[i]; slot = i;
        }
    }
    if (slot < 0) return;
    app.pd->sound->sampleplayer->stop(app.effect_players[slot]);
    app.pd->sound->sampleplayer->setSample(app.effect_players[slot], app.effects[event]);
    volume = sound_volume(event);
    app.pd->sound->sampleplayer->setVolume(app.effect_players[slot], volume, volume);
    app.pd->sound->sampleplayer->play(app.effect_players[slot], 1, 1.0f);
    app.effect_priority[slot] = (uint8_t)priority;
    app.effect_event[slot] = (uint8_t)event;
    app.effect_serial[slot] = ++app.next_effect_serial;
}

static int effect_is_playing(LPSoundEvent event) {
    int i;
    for (i = 0; i < EFFECT_VOICES; ++i)
        if (app.effect_event[i] == (uint8_t)event &&
            app.pd->sound->sampleplayer->isPlaying(app.effect_players[i])) return 1;
    return 0;
}

static int load_effects(void) {
    static const char* names[LP_SOUND_COUNT] = {
        [LP_SOUND_DOOR] = "door",
        [LP_SOUND_LETS_GO] = "letsgo",
        [LP_SOUND_SELECT] = "changeop",
        [LP_SOUND_ASSIGN] = "mousepre",
        [LP_SOUND_BUILDER_WARNING] = "ting",
        [LP_SOUND_YIPPEE] = "yippee",
        [LP_SOUND_DROWN] = "glug",
        [LP_SOUND_SPLAT] = "splat",
        [LP_SOUND_BURN] = "fire",
        [LP_SOUND_ELECTRIC] = "electric",
        [LP_SOUND_CHAIN] = "chain",
        [LP_SOUND_TRAP] = "tenton",
        [LP_SOUND_OH_NO] = "ohno",
        [LP_SOUND_EXPLODE] = "explode",
        [LP_SOUND_THUD] = "thud",
    };
    int i;
    for (i = 0; i < LP_SOUND_COUNT; ++i) {
        char path[40];
        if (!names[i]) continue;
        snprintf(path, sizeof path, "effects/%s", names[i]);
        app.effects[i] = app.pd->sound->sample->load(path);
        if (!app.effects[i]) return 0;
    }
    for (i = 0; i < EFFECT_VOICES; ++i) {
        app.effect_players[i] = app.pd->sound->sampleplayer->newPlayer();
        if (!app.effect_players[i]) return 0;
    }
    return 1;
}

static void find_content_bounds(void) {
    int x, y, left = LP_LEVEL_WIDTH, right = -1;
    unsigned i;
    for (y = 0; y < LP_LEVEL_HEIGHT; ++y) for (x = 0; x < LP_LEVEL_WIDTH; ++x)
        if (app.visual[y * LP_ROW_BYTES + x / 8] & (0x80 >> (x & 7))) {
            if (x < left) left = x;
            if (x > right) right = x;
        }
    for (i = 0; i < app.level.meta.object_count; ++i) {
        const LPObject* object = &app.level.objects[i];
        unsigned slot = app.level.meta.style * 16u + object->object_id;
        int object_right = object->x;
        if (slot < LP_OBJECT_SPRITES) object_right += app.sprites.object_sprites[slot].width - 1;
        if (object->x < left) left = object->x;
        if (object_right > right) right = object_right;
    }
    if (right < left) { left = 0; right = LP_LEVEL_WIDTH - 1; }
    app.content_left = left > CONTENT_PADDING ? left - CONTENT_PADDING : 0;
    app.content_right = right + CONTENT_PADDING < LP_LEVEL_WIDTH ?
                        right + CONTENT_PADDING : LP_LEVEL_WIDTH - 1;
}

static void horizontal_camera_bounds(int view_width, int* minimum, int* maximum) {
    int min = app.content_left;
    int max = app.content_right - view_width + 1;
    if (max < min) {
        min = (app.content_left + app.content_right - view_width + 1) / 2;
        if (min < 0) min = 0;
        if (min > LP_LEVEL_WIDTH - view_width) min = LP_LEVEL_WIDTH - view_width;
        max = min;
    }
    *minimum = min; *maximum = max;
}

static int load_level(int index) {
    lp_adlib_stop(&app.adlib);
    app.level.visual = app.visual; app.level.visual_bayer2 = app.visual_bayer2;
    app.level.visual_cluster2 = app.visual_cluster2; app.level.solid = app.solid;
    app.level.steel = app.steel;
    app.level.terrain_masks = app.sprites.terrain_masks;
    if (!lp_pack_load_level(&app.pack, (uint32_t)index, &app.level)) return 0;
    lp_game_init(&app.game, &app.level);
    find_content_bounds();
    app.level_index = index; app.camera_x = app.level.meta.camera_x;
    app.cursor_x = app.camera_x + (app.detail_mode ? 100 : 200);
    if (app.cursor_x < app.content_left) app.cursor_x = app.content_left;
    if (app.cursor_x > app.content_right) app.cursor_x = app.content_right;
    /* A level opens centred on its entrance hatch. The DOS start scroll was
       authored for a 320-pixel viewport and leaves the hatch out of frame in
       the 200-wide 2x detail view, so it is not used to place the opening
       view. Near a level edge the camera clamp below still applies, which
       moves the hatch off centre but keeps it on screen. */
    {
        unsigned i;
        for (i = 0; i < app.level.meta.object_count; ++i) {
            const LPObject* object = &app.level.objects[i];
            unsigned slot = app.level.meta.style * 16u + object->object_id;
            if (object->object_id != 1) continue;
            app.cursor_x = object->x +
                (slot < LP_OBJECT_SPRITES ? app.sprites.object_sprites[slot].width / 2 : 0);
            break;
        }
    }
    app.cursor_y = 80; app.camera_y = 0;
    app.selected_skill = LP_SKILL_CLIMBER; app.simulation_time = 0;
    app.music_index = lp_music_for_level((unsigned)index);
    app.intro_active = 0; app.music_started = 0;
    return 1;
}

static const char* rating_name(unsigned rating) {
    static const char* names[LP_RATING_COUNT] = {"FUN", "TRICKY", "TAXING", "MAYHEM"};
    return rating < LP_RATING_COUNT ? names[rating] : "FUN";
}

static int select_browser_level(unsigned rating, unsigned level) {
    if (!LP_TEST_BUILD && !lp_progress_can_select(app.save.completed, rating, level)) return 0;
    if (LP_TEST_BUILD && (rating >= LP_RATING_COUNT || level >= LP_LEVELS_PER_RATING)) return 0;
    if (!load_level((int)(rating * LP_LEVELS_PER_RATING + level))) return 0;
    app.browser_rating = (int)rating;
    app.browser_level = (int)level;
    return 1;
}

static void open_level_select(void) {
    unsigned rating, level;
    lp_adlib_stop(&app.adlib);
    rating = (unsigned)app.level_index / LP_LEVELS_PER_RATING;
    level = (unsigned)app.level_index % LP_LEVELS_PER_RATING;
    if (!LP_TEST_BUILD && !lp_progress_can_select(app.save.completed, rating, level))
        level = lp_progress_current(app.save.completed, rating);
    select_browser_level(rating, level);
    app.browser_crank = 0;
    set_levels_menu_visible(0);
    set_reset_menu_visible(0);
    set_dither_menu_visible(0);
    app.screen = SCREEN_LEVEL_SELECT;
}

static void begin_game(void) {
    lp_adlib_stop(&app.adlib);
    app.save.last_level = (uint8_t)app.level_index;
    save_progress();
    app.screen = SCREEN_GAME; app.game.paused = 0; app.pending_game_b = 0;
    set_levels_menu_visible(1);
    set_reset_menu_visible(1);
    app.simulation_time = 0; app.intro_active = 1; app.music_started = 0;
    app.pd->system->resetElapsedTime();
    play_effect(LP_SOUND_LETS_GO);
}

static void menu_levels(void* userdata) {
    (void)userdata;
    open_level_select();
}

static void menu_restart(void* userdata) {
    (void)userdata; load_level(app.level_index); begin_game();
}

static void menu_nuke(void* userdata) { (void)userdata; lp_game_nuke(&app.game); }

static void menu_game(void* userdata) {
    int action;
    (void)userdata;
    action = app.pd->system->getMenuItemValue(app.game_menu);
    if (action == 1) menu_nuke(NULL);
    app.pd->system->setMenuItemValue(app.game_menu, 0);
}

static void menu_ground(void* userdata) {
    (void)userdata;
    app.ground_mode = app.pd->system->getMenuItemValue(app.ground_menu);
}

static void load_progress(void) {
    SDFile* file;
    memset(&app.save, 0, sizeof app.save);
    memcpy(app.save.magic, "LPS1", 4);
    file = app.pd->file->open("save.dat", kFileReadData);
    if (file) {
        SaveData saved;
        if (app.pd->file->read(file, &saved, sizeof saved) == (int)sizeof saved &&
            memcmp(saved.magic, "LPS1", 4) == 0 && saved.last_level < 120) app.save = saved;
        app.pd->file->close(file);
    }
}

static void load_version(void) {
    SDFile* file = app.pd->file->open("pdxinfo", kFileRead);
    char buffer[256];
    int length;
    char* value;
    app.version[0] = '\0';
    if (!file) return;
    length = app.pd->file->read(file, buffer, sizeof buffer - 1);
    app.pd->file->close(file);
    if (length <= 0) return;
    buffer[length] = '\0';
    value = strstr(buffer, "version=");
    if (value) {
        int i = 0;
        value += strlen("version=");
        while (value[i] && value[i] != '\r' && value[i] != '\n' &&
               i < (int)sizeof app.version - 1) {
            app.version[i] = value[i];
            ++i;
        }
        app.version[i] = '\0';
    }
}

static void save_progress(void) {
    SDFile* file;
    /* A test build must leave the player's achieved progression untouched. */
    if (LP_TEST_BUILD) return;
    file = app.pd->file->open("save.dat", kFileWrite);
    if (!file) return;
    app.pd->file->write(file, &app.save, sizeof app.save);
    app.pd->file->close(file);
}

static int world_ink(int x, int y) {
    const uint8_t* plane;
    if (x < 0 || x >= LP_LEVEL_WIDTH || y < 0 || y >= LP_LEVEL_HEIGHT) return 0;
    plane = app.ground_mode == 0 ? app.solid :
            app.ground_mode == 1 ? app.visual_bayer2 :
            app.ground_mode == 2 ? app.visual_cluster2 : app.visual;
    return !!(plane[y * LP_ROW_BYTES + x / 8] & (0x80 >> (x & 7)));
}

static int world_solid(int x, int y) {
    if (x < 0 || x >= LP_LEVEL_WIDTH || y < 0 || y >= LP_LEVEL_HEIGHT) return 0;
    return !!(app.solid[y * LP_ROW_BYTES + x / 8] & (0x80 >> (x & 7)));
}

static void set_pixel(uint8_t* frame, int x, int y, int black) {
    uint8_t mask;
    if (x < 0 || x >= LCD_COLUMNS || y < 0 || y >= LCD_ROWS) return;
    mask = (uint8_t)(0x80 >> (x & 7));
    if (black) frame[y * LCD_ROWSIZE + x / 8] &= (uint8_t)~mask;
    else frame[y * LCD_ROWSIZE + x / 8] |= mask;
}

static int world_origin_y(void) {
    int scale = app.detail_mode ? 2 : 1;
    int view_height = app.detail_mode ? (LCD_ROWS - HUD_HEIGHT) / scale : LP_LEVEL_HEIGHT;
    return HUD_HEIGHT + (LCD_ROWS - HUD_HEIGHT - view_height * scale) / 2;
}

static void draw_world_pixel(uint8_t* frame, int x, int y, int black) {
    int scale = app.detail_mode ? 2 : 1;
    int origin_y = world_origin_y();
    int xx, yy;
    for (yy = 0; yy < scale; ++yy) for (xx = 0; xx < scale; ++xx)
        set_pixel(frame, x * scale + xx, origin_y + y * scale + yy, black);
}

static void render_hud(void) {
    static const char* skills[] = {
        "CLIMBER", "FLOATER", "BOMBER", "BLOCKER",
        "BUILDER", "BASHER", "MINER", "DIGGER"
    };
    char line[96];
    unsigned seconds = (app.game.remaining_ticks + LP_TICKS_PER_SECOND - 1) /
                       LP_TICKS_PER_SECOND;
    app.pd->graphics->setFont(app.game_font);
    app.pd->graphics->setDrawMode(kDrawModeCopy);
    app.pd->graphics->fillRect(0, 0, LCD_COLUMNS, HUD_HEIGHT, kColorWhite);
    snprintf(line, sizeof line, "%02u:%02u", seconds / 60, seconds % 60);
    app.pd->graphics->drawText(line, strlen(line), kASCIIEncoding, 8, 1);
    snprintf(line, sizeof line, "OUT %u/%u", app.game.released,
             app.level.meta.lemming_count);
    app.pd->graphics->drawText(line, strlen(line), kASCIIEncoding, 64, 1);
    snprintf(line, sizeof line, "SAVED %u/%u", app.game.rescued,
             app.level.meta.rescue_count);
    app.pd->graphics->drawText(line, strlen(line), kASCIIEncoding, 151, 1);
    if (app.game.paused) snprintf(line, sizeof line, "PAUSED");
    else snprintf(line, sizeof line, "%s %03u", skills[app.selected_skill],
                  app.level.meta.skills[app.selected_skill]);
    app.pd->graphics->drawText(line, strlen(line), kASCIIEncoding, 250, 1);
    app.pd->graphics->drawLine(0, HUD_HEIGHT - 1, LCD_COLUMNS - 1,
                               HUD_HEIGHT - 1, 1, kColorBlack);
}

static void draw_bomber_countdown(uint8_t* frame, const LPLemming* lemming) {
    static const uint8_t digits[5][5] = {
        {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
        {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}
    };
    unsigned digit;
    int x, y, xx, yy, sx, sy;
    if (!lemming->countdown) return;
    digit = (lemming->countdown + LP_TICKS_PER_SECOND - 1) / LP_TICKS_PER_SECOND;
    if (digit < 1) digit = 1;
    if (digit > LP_BOMBER_SECONDS) digit = LP_BOMBER_SECONDS;
    sx = lemming->x - 1 - app.camera_x;
    sy = lemming->y - 19 - app.camera_y;
    /* A one-pixel white keyline keeps the original floating digit legible on
       every monochrome terrain pattern. */
    for (y = 0; y < 5; ++y) for (x = 0; x < 3; ++x)
        if (digits[digit - 1][y] & (4 >> x))
            for (yy = -1; yy <= 1; ++yy) for (xx = -1; xx <= 1; ++xx)
                draw_world_pixel(frame, sx + x + xx, sy + y + yy, 0);
    for (y = 0; y < 5; ++y) for (x = 0; x < 3; ++x)
        if (digits[digit - 1][y] & (4 >> x))
            draw_world_pixel(frame, sx + x, sy + y, 1);
}

static void draw_explosion_particles(uint8_t* frame, const LPLemming* lemming) {
    const uint8_t* particles;
    unsigned index;
    if (!app.sprites.explosion_particles || lemming->frame < 1 ||
        lemming->frame > LP_EXPLOSION_FRAMES) return;
    particles = app.sprites.explosion_particles +
                (lemming->frame - 1u) * LP_EXPLOSION_PARTICLES * 2u;
    for (index = 0; index < LP_EXPLOSION_PARTICLES; ++index) {
        int dx = (int8_t)particles[index * 2];
        int dy = (int8_t)particles[index * 2 + 1];
        if (dx == -128 && dy == -128) continue;
        draw_world_pixel(frame, lemming->x + dx - app.camera_x,
                         lemming->y + dy - app.camera_y, 1);
    }
}

static void draw_lemming(uint8_t* frame, const LPLemming* lemming) {
    unsigned slot = lemming->state * 2u + (lemming->right ? 0u : 1u);
    const LPSprite* sprite;
    const uint8_t *mask, *ink;
    int frame_index, x, y, sx, sy;
    if (lemming->state == LP_STATE_EXPLODE && lemming->frame > 0) {
        draw_explosion_particles(frame, lemming);
        return;
    }
    if (slot >= LP_SPRITE_SLOTS) return;
    sprite = &app.sprites.sprites[slot];
    if (!sprite->frame_count) return;
    frame_index = (int)lp_game_sprite_frame(lemming, sprite->frame_count);
    mask = app.sprites.data + sprite->data_offset + frame_index * sprite->frame_bytes;
    ink = mask + sprite->frame_bytes / 2;
    sx = lemming->x + sprite->offset_x - app.camera_x;
    sy = lemming->y + sprite->offset_y - app.camera_y;
    for (y = 0; y < sprite->height; ++y) for (x = 0; x < sprite->width; ++x) {
        uint8_t bit = (uint8_t)(0x80 >> (x & 7));
        int at = y * sprite->row_bytes + x / 8;
        if (!(mask[at] & bit)) continue;
        draw_world_pixel(frame, sx + x, sy + y, !!(ink[at] & bit));
    }
    draw_bomber_countdown(frame, lemming);
}

static void draw_object(uint8_t* frame, unsigned index) {
    const LPObject* object = &app.level.objects[index];
    unsigned slot = app.level.meta.style * 16u + object->object_id;
    const LPSprite* sprite;
    const uint8_t *mask, *ink;
    int frame_index, x, y, sx, sy, plane_bytes, portal;
    if (slot >= LP_OBJECT_SPRITES) return;
    sprite = &app.sprites.object_sprites[slot];
    if (!sprite->frame_count) return;
    plane_bytes = sprite->row_bytes * sprite->height;
    portal = (object->object_id == 1 || object->trigger_effect == 1) &&
             sprite->frame_bytes >= plane_bytes * 5;
    frame_index = (int)lp_game_object_frame(&app.game, index) % sprite->frame_count;
    mask = app.sprites.data + sprite->data_offset + frame_index * sprite->frame_bytes;
    ink = mask + plane_bytes * (portal ? app.ground_mode + 1 : 1);
    sx = object->x - app.camera_x;
    sy = object->y - app.camera_y;
    for (y = 0; y < sprite->height; ++y) for (x = 0; x < sprite->width; ++x) {
        int source_y = (object->flags & 1) ? sprite->height - y - 1 : y;
        int at = source_y * sprite->row_bytes + x / 8;
        int world_x = object->x + x, world_y = object->y + y;
        uint8_t bit = (uint8_t)(0x80 >> (x & 7));
        if (!(mask[at] & bit)) continue;
        if ((object->flags & 2) && world_solid(world_x, world_y)) continue;
        if ((object->flags & 4) && !world_solid(world_x, world_y)) continue;
        draw_world_pixel(frame, sx + x, sy + y, !!(ink[at] & bit));
    }
}

static void render_world(void) {
    uint8_t* frame = app.pd->graphics->getFrame();
    int x, y, view_width, view_height, min_x, max_x, max_y, scale, origin_y;
    memset(frame, 0xFF, LCD_ROWSIZE * LCD_ROWS);
    scale = app.detail_mode ? 2 : 1;
    origin_y = world_origin_y();
    view_width = LCD_COLUMNS / scale;
    view_height = app.detail_mode ? (LCD_ROWS - HUD_HEIGHT) / scale : LP_LEVEL_HEIGHT;
    horizontal_camera_bounds(view_width, &min_x, &max_x);
    max_y = LP_LEVEL_HEIGHT - view_height;
    app.camera_x = app.cursor_x - view_width / 2;
    app.camera_y = app.detail_mode ? app.cursor_y - view_height / 2 : 0;
    if (app.camera_x < min_x) app.camera_x = min_x;
    if (app.camera_x > max_x) app.camera_x = max_x;
    if (app.camera_y < 0) app.camera_y = 0;
    if (app.camera_y > max_y) app.camera_y = max_y;
    for (y = 0; y < view_height; ++y) for (x = 0; x < view_width; ++x)
        if (world_ink(app.camera_x + x, app.camera_y + y))
            draw_world_pixel(frame, x, y, 1);
    for (x = 0; x < app.level.meta.object_count; ++x) draw_object(frame, (unsigned)x);
    for (x = 0; x < LP_MAX_LEMMINGS; ++x)
        if (app.game.lemmings[x].active) draw_lemming(frame, &app.game.lemmings[x]);
    {
        int selected = lp_game_select_at(&app.game, app.cursor_x, app.cursor_y);
        int cx = (app.cursor_x - app.camera_x) * scale + scale / 2;
        int cy = origin_y + (app.cursor_y - app.camera_y) * scale + scale / 2;
        app.pd->graphics->setDrawMode(kDrawModeCopy);
        if (selected >= 0 && lp_game_can_assign(
                &app.game, (unsigned)selected, (LPSkill)app.selected_skill)) {
            const LPLemming* lemming = &app.game.lemmings[selected];
            int center_y = lemming->y + LP_SELECT_CENTER_Y_OFFSET;
            int left = (lemming->x - LP_SELECT_RADIUS_X - app.camera_x) * scale;
            int right = (lemming->x + LP_SELECT_RADIUS_X + 1 - app.camera_x) * scale - 1;
            int top = origin_y + (center_y - LP_SELECT_RADIUS_Y - app.camera_y) * scale;
            int bottom = origin_y + (center_y + LP_SELECT_RADIUS_Y + 1 - app.camera_y) * scale - 1;
            app.pd->graphics->drawRect(left, top, right - left + 1, bottom - top + 1,
                                       kColorBlack);
            app.pd->graphics->drawRect(left + 1, top + 1, right - left - 1,
                                       bottom - top - 1, kColorBlack);
        }
        app.pd->graphics->setDrawMode(kDrawModeXOR);
        app.pd->graphics->drawLine(cx - 11, cy, cx + 11, cy, 2, kColorXOR);
        app.pd->graphics->drawLine(cx, cy - 11, cx, cy + 11, 2, kColorXOR);
        app.pd->graphics->setDrawMode(kDrawModeCopy);
    }
    render_hud();
    app.pd->graphics->markUpdatedRows(0, LCD_ROWS - 1);
}

static void render_logo(void) {
    char line[48];
    int width = 0, height = 0;
    app.pd->graphics->setFont(app.font);
    app.pd->graphics->clear(kColorWhite);
    if (app.logo) {
        app.pd->graphics->getBitmapData(app.logo, &width, &height, NULL, NULL, NULL);
        app.pd->graphics->drawBitmap(app.logo, (LCD_COLUMNS - width) / 2,
                                     50, kBitmapUnflipped);
    }
    draw_centered_text("ORIGINAL GAME BY DMA DESIGN", 164);
    draw_centered_text("DOS PORT FOR PLAYDATE BY BSTAR", 182);
    snprintf(line, sizeof line, "A LEVEL SELECT   B CREDITS");
    width = app.pd->graphics->getTextWidth(app.font, line, strlen(line), kASCIIEncoding, 0);
    app.pd->graphics->drawText(line, strlen(line), kASCIIEncoding,
                               (LCD_COLUMNS - width) / 2, 210);
    if (app.version[0]) {
        snprintf(line, sizeof line, "V%s", app.version);
        width = app.pd->graphics->getTextWidth(app.font, line, strlen(line), kASCIIEncoding, 0);
        app.pd->graphics->drawText(line, strlen(line), kASCIIEncoding,
                                   LCD_COLUMNS - width - 6, 3);
    }
}

static void render_credits(void) {
    app.pd->graphics->setFont(app.font);
    app.pd->graphics->clear(kColorWhite);
    draw_centered_text("ORIGINAL LEMMINGS - 1991", 4);
    draw_centered_text("ORIGINAL GAME BY DMA DESIGN", 23);
    draw_centered_text("PC PROGRAMMING: RUSSELL KAY", 48);
    draw_centered_text("ANIMATION: GARY TIMMONS", 66);
    draw_centered_text("BACKGROUNDS: SCOTT JOHNSTON", 84);
    draw_centered_text("LEVELS: MIKE DAILLY / GARY TIMMONS", 102);
    draw_centered_text("SCOTT JOHNSTON / DAVE JONES", 120);
    draw_centered_text("MUSIC & SFX: BRIAN JOHNSTON", 138);
    draw_centered_text("MUSIC: TIM WRIGHT / TONY WILLIAMS", 156);
    draw_centered_text("PUBLISHED BY PSYGNOSIS", 181);
    draw_centered_text("DOS PORT FOR PLAYDATE BY BSTAR", 199);
    app.pd->graphics->drawText("B BACK", 6, kASCIIEncoding, 6, 217);
}

static void draw_centered_text(const char* text, int y) {
    int width = app.pd->graphics->getTextWidth(app.font, text, strlen(text),
                                               kASCIIEncoding, 0);
    app.pd->graphics->drawText(text, strlen(text), kASCIIEncoding,
                               (LCD_COLUMNS - width) / 2, y);
}

static const uint8_t* preview_plane(void) {
    return app.ground_mode == 0 ? app.solid :
           app.ground_mode == 1 ? app.visual_bayer2 :
           app.ground_mode == 2 ? app.visual_cluster2 : app.visual;
}

static void render_level_preview(uint8_t* frame, int px, int py, int width, int height) {
    const uint8_t* plane = preview_plane();
    int source_width = app.content_right - app.content_left + 1;
    int x, y;
    app.pd->graphics->drawRect(px - 1, py - 1, width + 2, height + 2, kColorBlack);
    for (y = 0; y < height; ++y) for (x = 0; x < width; ++x) {
        int x0 = app.content_left + x * source_width / width;
        int x1 = app.content_left + (x + 1) * source_width / width;
        int y0 = y * LP_LEVEL_HEIGHT / height;
        int y1 = (y + 1) * LP_LEVEL_HEIGHT / height;
        int sx, sy, ink = 0;
        if (x1 <= x0) x1 = x0 + 1;
        if (y1 <= y0) y1 = y0 + 1;
        for (sy = y0; sy < y1 && !ink; ++sy) for (sx = x0; sx < x1; ++sx)
            if (plane[sy * LP_ROW_BYTES + sx / 8] & (0x80 >> (sx & 7))) {
                ink = 1; break;
            }
        if (ink) set_pixel(frame, px + x, py + y, 1);
    }
}

static void render_level_select(void) {
    char line[96];
    unsigned completed = lp_progress_completed_count(app.save.completed,
                                                      (unsigned)app.browser_rating);
    int i;
    app.pd->graphics->setFont(app.font);
    app.pd->graphics->clear(kColorWhite);
    for (i = 0; i < (int)LP_RATING_COUNT; ++i) {
        int x = 7 + i * 98;
        const char* name = rating_name((unsigned)i);
        int text_width = app.pd->graphics->getTextWidth(app.font, name, strlen(name),
                                                        kASCIIEncoding, 0);
        if (i == app.browser_rating) app.pd->graphics->fillRoundRect(x, 4, 92, 21, 4, kColorBlack);
        app.pd->graphics->setDrawMode(i == app.browser_rating ? kDrawModeInverted : kDrawModeCopy);
        app.pd->graphics->drawText(name, strlen(name), kASCIIEncoding,
                                   x + (92 - text_width) / 2, 7);
    }
    app.pd->graphics->setDrawMode(kDrawModeCopy);
    app.pd->graphics->drawRoundRect(13, 29, 374, 193, 7, 2, kColorBlack);
    snprintf(line, sizeof line, "%s %02d / 30", rating_name((unsigned)app.browser_rating),
             app.browser_level + 1);
    app.pd->graphics->drawText(line, strlen(line), kASCIIEncoding, 25, 37);
    snprintf(line, sizeof line, "%s", lp_progress_completed(app.save.completed,
             (unsigned)app.level_index) ? "COMPLETED" : "CURRENT");
    app.pd->graphics->drawText(line, strlen(line), kASCIIEncoding,
                               373 - app.pd->graphics->getTextWidth(app.font, line,
                               strlen(line), kASCIIEncoding, 0), 37);
    app.pd->graphics->drawTextInRect(app.level.meta.name, strlen(app.level.meta.name),
        kASCIIEncoding, 25, 59, 350, 29, kWrapWord, kAlignTextCenter);
    render_level_preview(app.pd->graphics->getFrame(), 29, 91, 342, 65);
    snprintf(line, sizeof line, "OUT %03u  NEED %03u  TIME %02u:00",
             app.level.meta.lemming_count, app.level.meta.rescue_count,
             app.level.meta.time_minutes);
    draw_centered_text(line, 164);
    snprintf(line, sizeof line, "PROGRESS %02u / 30", completed);
    draw_centered_text(line, 184);
    if (LP_TEST_BUILD) draw_centered_text("TEST BUILD - PROGRESS NOT SAVED", 202);
    else draw_centered_text("UP/DOWN DIFFICULTY  LEFT/RIGHT LEVEL", 202);
    draw_centered_text("A START   B BACK   CRANK LEVEL", 225);
    app.pd->graphics->markUpdatedRows(0, LCD_ROWS - 1);
}

static void render_result(void) {
    char line[96];
    int won = lp_game_won(&app.game);
    int mastered = won && app.level_index == (int)(LP_RATING_COUNT * LP_LEVELS_PER_RATING - 1);
    app.pd->graphics->setFont(app.font);
    app.pd->graphics->clear(kColorWhite);
    if (mastered) {
        draw_centered_text("CONGRATULATIONS!", 29);
        draw_centered_text("EVERYBODY HERE AT DMA DESIGN", 62);
        draw_centered_text("SALUTES YOU AS A MASTER", 81);
        draw_centered_text("LEMMINGS PLAYER", 100);
        snprintf(line, sizeof line, "RESCUED %u / %u", app.game.rescued,
                 app.level.meta.rescue_count);
        draw_centered_text(line, 133);
        draw_centered_text("A LEVEL SELECT   B RETRY", 170);
        return;
    }
    app.pd->graphics->drawText(won ? "LEVEL COMPLETE" : "LEVEL FAILED",
        won ? 14 : 12, kASCIIEncoding, won ? 133 : 142, 55);
    snprintf(line, sizeof line, "RESCUED %u / %u", app.game.rescued, app.level.meta.rescue_count);
    app.pd->graphics->drawText(line, strlen(line), kASCIIEncoding, 132, 102);
    app.pd->graphics->drawText(won ? "A: NEXT LEVEL    B: RETRY" : "A/B: RETRY",
        won ? 25 : 10, kASCIIEncoding, won ? 92 : 152, 155);
    app.pd->graphics->drawText("MENU: RETURN TO LEVEL SELECT", 28, kASCIIEncoding, 84, 195);
}

static void draw_action_tile(uint8_t* frame, unsigned action, int origin_x, int origin_y) {
    const uint32_t* rows = action < 2 ? lynx_release_rows :
                           action == 11 ? lynx_nuke_rows : NULL;
    int x, y, xx, yy;
    if (!rows) {
        /* Pause is the only control absent from the supplied Lynx panel. */
        for (y = 5; y < 27; ++y) for (x = 10; x < 22; ++x)
            if (x < 15 || x >= 17)
                for (yy = 0; yy < 2; ++yy) for (xx = 0; xx < 2; ++xx)
                    set_pixel(frame, origin_x + x * 2 + xx, origin_y + y * 2 + yy, 1);
        return;
    }
    for (y = 0; y < 32; ++y) for (x = 0; x < 32; ++x) {
        int black = !!(rows[y] & (0x80000000u >> x));
        if (black)
            for (yy = 0; yy < 2; ++yy) for (xx = 0; xx < 2; ++xx)
                set_pixel(frame, origin_x + x * 2 + xx, origin_y + y * 2 + yy, 1);
    }
}

static void draw_skill_action(uint8_t* frame, unsigned action, int selected,
                              int origin_x, int origin_y) {
    static const LPLemmingState states[8] = {
        LP_STATE_CLIMB, LP_STATE_FLOAT, LP_STATE_OHNO, LP_STATE_BLOCK,
        LP_STATE_BUILD, LP_STATE_BASH, LP_STATE_MINE, LP_STATE_DIG
    };
    static const uint8_t idle_frames[8] = { 3, 7, 5, 4, 9, 11, 7, 3 };
    LPLemming animation;
    const LPSprite* sprite;
    const uint8_t* mask;
    unsigned logical_frame, frame_index, slot;
    int scale = 4, x, y, xx, yy, draw_x, draw_y;
    if (action < 2 || action >= 10) {
        draw_action_tile(frame, action, origin_x, origin_y);
        return;
    }
    memset(&animation, 0, sizeof animation);
    animation.state = (uint8_t)states[action - 2];
    slot = animation.state * 2u;
    sprite = &app.sprites.sprites[slot];
    if (!sprite->frame_count) return;
    if (selected) {
        logical_frame = app.action_animation_tick;
        if (animation.state == LP_STATE_FLOAT && logical_frame >= 16)
            logical_frame = 8 + (logical_frame - 16) % 8;
        animation.frame = (uint8_t)logical_frame;
        frame_index = lp_game_sprite_frame(&animation, sprite->frame_count);
    } else {
        frame_index = idle_frames[action - 2] % sprite->frame_count;
    }
    mask = app.sprites.data + sprite->data_offset + frame_index * sprite->frame_bytes;
    draw_x = origin_x + (64 - sprite->width * scale) / 2;
    draw_y = origin_y + (64 - sprite->height * scale) / 2;
    for (y = 0; y < sprite->height; ++y) for (x = 0; x < sprite->width; ++x) {
        int at = y * sprite->row_bytes + x / 8;
        if (!(mask[at] & (0x80 >> (x & 7)))) continue;
        for (yy = 0; yy < scale; ++yy) for (xx = 0; xx < scale; ++xx)
            set_pixel(frame, draw_x + x * scale + xx, draw_y + y * scale + yy, 1);
    }
}

static void draw_action_digit(uint8_t* frame, unsigned digit, unsigned position,
                              int origin_x, int origin_y) {
    const uint8_t* glyph = app.sprites.action_digits + (digit * 2u + position) * 8u;
    int x, y, xx, yy;
    for (y = 0; y < 8; ++y) for (x = 0; x < 8; ++x)
        if (glyph[y] & (0x80 >> x))
            for (yy = 0; yy < 2; ++yy) for (xx = 0; xx < 2; ++xx)
                set_pixel(frame, origin_x + x * 2 + xx, origin_y + y * 2 + yy, 1);
}

static void draw_action_count(uint8_t* frame, unsigned value, int origin_x, int origin_y) {
    if (value > 99) value = 99;
    draw_action_digit(frame, value / 10, 0, origin_x, origin_y);
    draw_action_digit(frame, value % 10, 1, origin_x + 16, origin_y);
}

static void draw_centered_game_text(const char* text, int y) {
    int width = app.pd->graphics->getTextWidth(app.game_font, text, strlen(text),
                                               kASCIIEncoding, 0);
    app.pd->graphics->drawText(text, strlen(text), kASCIIEncoding,
                               (LCD_COLUMNS - width) / 2, y);
}

static void draw_centered_region_game_text(const char* text, int left, int width, int y) {
    int text_width = app.pd->graphics->getTextWidth(app.game_font, text, strlen(text),
                                                    kASCIIEncoding, 0);
    app.pd->graphics->drawText(text, strlen(text), kASCIIEncoding,
                               left + (width - text_width) / 2, y);
}

static void render_actions(void) {
    static const char* names[LP_ACTION_COUNT] = {
        "RELEASE RATE", "", "CLIMBER", "FLOATER",
        "BOMBER", "BLOCKER", "BUILDER", "BASHER", "MINER", "DIGGER",
        "PAUSE", "NUKE ALL"
    };
    uint8_t* frame;
    char line[48];
    int action;
    app.pd->graphics->setFont(app.game_font);
    app.pd->graphics->setDrawMode(kDrawModeCopy);
    app.pd->graphics->clear(kColorWhite);
    frame = app.pd->graphics->getFrame();
    for (action = 0; action < LP_ACTION_COUNT; ++action) {
        int column = action % 6, row = action / 6;
        int x = 8 + column * 64, count_y = 25 + row * 96;
        int icon_y = 46 + row * 96;
        unsigned count = 0;
        if (action == 1) continue;
        if (action == 0) {
            draw_action_count(frame, app.game.release_rate, x + 48, count_y);
            draw_centered_region_game_text("RELEASE RATE", x, 128, icon_y + 8);
            draw_centered_region_game_text(app.release_rate_editing ? "HOLD UP/DOWN" : "A EDIT",
                                           x, 128, icon_y + 31);
        } else if (action >= 2 && action < 10)
            draw_skill_action(frame, (unsigned)action, action == app.action_menu_item,
                              x, icon_y);
        else
            draw_action_tile(frame, (unsigned)action, x, icon_y);
        if (action >= 2 && action < 10) count = app.level.meta.skills[action - 2];
        if (action >= 2 && action < 10) draw_action_count(frame, count, x + 16, count_y);
        if (action == app.action_menu_item) {
            int selection_width = action == 0 ? 126 : 62;
            app.pd->graphics->drawRoundRect(x + 1, 21 + row * 96, selection_width, 90, 4, 2,
                                            kColorBlack);
        }
    }
    if (app.release_rate_editing)
        snprintf(line, sizeof line, "RELEASE RATE  %02u", app.game.release_rate);
    else if (app.action_menu_item == 10)
        snprintf(line, sizeof line, "%s", app.menu_was_paused ? "RESUME GAME" : "PAUSE GAME");
    else if (app.action_menu_item == 11 && app.game.nuking)
        snprintf(line, sizeof line, "NUKE ACTIVE");
    else if (app.action_menu_item == 11 && app.nuke_confirm)
        snprintf(line, sizeof line, "PRESS A AGAIN: NUKE ALL");
    else if (app.action_menu_item == 0)
        snprintf(line, sizeof line, "%s  %02u", names[app.action_menu_item],
                 app.game.release_rate);
    else if (app.action_menu_item < 10)
        snprintf(line, sizeof line, "%s  %02u", names[app.action_menu_item],
                 app.level.meta.skills[app.action_menu_item - 2]);
    else
        snprintf(line, sizeof line, "%s", names[app.action_menu_item]);
    draw_centered_game_text(line, 4);
    if (app.release_rate_editing)
        draw_centered_game_text("HOLD UP/DOWN   A DONE   B CANCEL", 220);
    else
        draw_centered_game_text("A SELECT   B BACK   CRANK RR", 220);
    app.pd->graphics->markUpdatedRows(0, LCD_ROWS - 1);
}

static void move_cursor(PDButtons current) {
    int dx = !!(current & kButtonRight) - !!(current & kButtonLeft);
    int dy = !!(current & kButtonDown) - !!(current & kButtonUp);
    app.cursor_speed = (dx || dy) ? (app.cursor_speed < 6 ? app.cursor_speed + 1 : 6) : 1;
    app.cursor_x += dx * app.cursor_speed; app.cursor_y += dy * app.cursor_speed;
    if (app.cursor_x < app.content_left) app.cursor_x = app.content_left;
    if (app.cursor_x > app.content_right) app.cursor_x = app.content_right;
    if (app.cursor_y < 0) app.cursor_y = 0;
    if (app.cursor_y >= LP_LEVEL_HEIGHT) app.cursor_y = LP_LEVEL_HEIGHT - 1;
}

static void move_browser_level(int direction) {
    unsigned next;
    if (LP_TEST_BUILD) {
        int candidate = app.browser_level + (direction > 0 ? 1 : direction < 0 ? -1 : 0);
        if (candidate < 0 || candidate >= (int)LP_LEVELS_PER_RATING) return;
        next = (unsigned)candidate;
    } else next = lp_progress_move(app.save.completed, (unsigned)app.browser_rating,
                                   (unsigned)app.browser_level, direction);
    if (next != (unsigned)app.browser_level &&
        select_browser_level((unsigned)app.browser_rating, next))
        play_effect(LP_SOUND_SELECT);
}

static void move_browser_rating(int direction) {
    int rating = app.browser_rating + direction;
    unsigned level;
    if (rating < 0 || rating >= (int)LP_RATING_COUNT) return;
    level = LP_TEST_BUILD ? (unsigned)app.browser_level
                          : lp_progress_current(app.save.completed, (unsigned)rating);
    if (select_browser_level((unsigned)rating, level)) play_effect(LP_SOUND_SELECT);
}

static int update(void* userdata) {
    PDButtons current, pushed, released;
    float elapsed, crank_change, crank_time;
    (void)userdata; (void)released;
    lp_adlib_pump(&app.adlib, LP_ADLIB_PUMP_FRAMES);
    app.pd->system->getButtonState(&current, &pushed, &released);
    if (app.screen == SCREEN_LOGO) {
        if (pushed & kButtonA) open_level_select();
        else if (pushed & kButtonB) app.screen = SCREEN_CREDITS;
        if (app.screen == SCREEN_LOGO) render_logo();
        else if (app.screen == SCREEN_CREDITS) render_credits();
        else render_level_select();
        return 1;
    }
    if (app.screen == SCREEN_CREDITS) {
        if (pushed & kButtonB) app.screen = SCREEN_LOGO;
        if (app.screen == SCREEN_CREDITS) render_credits();
        else render_logo();
        return 1;
    }
    if (app.screen == SCREEN_LEVEL_SELECT) {
        if (pushed & kButtonLeft) move_browser_level(-1);
        if (pushed & kButtonRight) move_browser_level(1);
        if (pushed & kButtonUp) move_browser_rating(-1);
        if (pushed & kButtonDown) move_browser_rating(1);
        app.browser_crank += app.pd->system->getCrankChange();
        while (app.browser_crank >= 30.0f) {
            move_browser_level(1); app.browser_crank -= 30.0f;
        }
        while (app.browser_crank <= -30.0f) {
            move_browser_level(-1); app.browser_crank += 30.0f;
        }
        if (pushed & kButtonA) begin_game();
        else if (pushed & kButtonB) {
            app.screen = SCREEN_LOGO;
            set_dither_menu_visible(1);
        }
        if (app.screen == SCREEN_LEVEL_SELECT) render_level_select();
        else if (app.screen == SCREEN_LOGO) render_logo();
        return 1;
    }
    if (app.screen == SCREEN_ACTIONS) {
        float action_elapsed = app.pd->system->getElapsedTime();
        int release_change = (int)(app.pd->system->getCrankChange() / 4.0f);
        int old_item = app.action_menu_item;
        /* Do not let elapsed wall time in the menu become a catch-up burst on
         * the first gameplay frame after closing it. */
        app.pd->system->resetElapsedTime();
        app.action_animation_time += action_elapsed;
        while (app.action_animation_time >= 1.0f / LP_TICKS_PER_SECOND) {
            ++app.action_animation_tick;
            app.action_animation_time -= 1.0f / LP_TICKS_PER_SECOND;
        }
        if (app.release_rate_editing) {
            int direction = !!(current & kButtonUp) - !!(current & kButtonDown);
            if (direction != app.release_repeat_direction) {
                app.release_repeat_direction = direction;
                app.release_repeat_time = RELEASE_REPEAT_DELAY;
                if (direction) lp_game_adjust_release_rate(&app.game, direction);
            } else if (direction) {
                app.release_repeat_time -= action_elapsed;
                while (app.release_repeat_time <= 0) {
                    lp_game_adjust_release_rate(&app.game, direction);
                    app.release_repeat_time += RELEASE_REPEAT_INTERVAL;
                }
            }
        } else {
            if (pushed & kButtonLeft) {
                if (app.action_menu_item == 2) app.action_menu_item = 0;
                else if (app.action_menu_item && app.action_menu_item % 6 > 0)
                    --app.action_menu_item;
            }
            if (pushed & kButtonRight) {
                if (app.action_menu_item == 0) app.action_menu_item = 2;
                else if (app.action_menu_item % 6 < 5) ++app.action_menu_item;
            }
            if ((pushed & kButtonUp) && app.action_menu_item >= 6) {
                if (app.action_menu_item < 8) app.action_menu_item = 0;
                else app.action_menu_item -= 6;
            }
            if (pushed & kButtonDown) {
                if (app.action_menu_item == 0) app.action_menu_item = 6;
                else if (app.action_menu_item < 6) app.action_menu_item += 6;
            }
        }
        if (old_item != app.action_menu_item) {
            app.nuke_confirm = 0;
            app.action_animation_tick = 0; app.action_animation_time = 0;
            play_effect(LP_SOUND_SELECT);
        }
        if (release_change) lp_game_adjust_release_rate(&app.game, release_change);
        if (app.release_rate_editing && (pushed & kButtonB)) {
            lp_game_adjust_release_rate(&app.game,
                app.release_rate_before_edit - app.game.release_rate);
            app.release_rate_editing = 0;
            app.release_repeat_direction = 0; app.release_repeat_time = 0;
            play_effect(LP_SOUND_SELECT);
        } else if (app.release_rate_editing && (pushed & kButtonA)) {
            app.release_rate_editing = 0;
            app.release_repeat_direction = 0; app.release_repeat_time = 0;
            play_effect(LP_SOUND_SELECT);
        } else if (pushed & kButtonB) {
            app.game.paused = app.menu_was_paused;
            app.screen = SCREEN_GAME; app.nuke_confirm = 0;
        } else if (pushed & kButtonA) {
            if (app.action_menu_item == 0) {
                app.release_rate_before_edit = app.game.release_rate;
                app.release_rate_editing = 1;
                app.release_repeat_direction = 0; app.release_repeat_time = 0;
                play_effect(LP_SOUND_SELECT);
            } else if (app.action_menu_item < 10) {
                app.selected_skill = app.action_menu_item - 2; play_effect(LP_SOUND_SELECT);
                app.game.paused = app.menu_was_paused;
                app.screen = SCREEN_GAME;
            } else if (app.action_menu_item == 10) {
                app.game.paused = !app.menu_was_paused;
                app.screen = SCREEN_GAME; play_effect(LP_SOUND_SELECT);
            } else if (!app.game.nuking && app.nuke_confirm) {
                lp_game_nuke(&app.game); app.nuke_confirm = 0;
                app.game.paused = app.menu_was_paused;
                app.screen = SCREEN_GAME;
            } else if (!app.game.nuking) {
                app.nuke_confirm = 1; play_effect(LP_SOUND_THUD);
            }
        }
        if (app.screen == SCREEN_ACTIONS) render_actions();
        else render_world();
        return 1;
    }
    if (app.screen == SCREEN_RESULT) {
        if (pushed & (kButtonA | kButtonB)) {
            if (lp_game_won(&app.game) && (pushed & kButtonA)) {
                unsigned rating = (unsigned)app.level_index / LP_LEVELS_PER_RATING;
                unsigned level = (unsigned)app.level_index % LP_LEVELS_PER_RATING;
                unsigned next = LP_TEST_BUILD
                    ? (level + 1u < LP_LEVELS_PER_RATING ? level + 1u : level)
                    : lp_progress_move(app.save.completed, rating, level, 1);
                if (next == level) open_level_select();
                else { select_browser_level(rating, next); begin_game(); }
            } else {
                load_level(app.level_index); begin_game();
            }
        }
        if (app.screen == SCREEN_RESULT) render_result();
        else if (app.screen == SCREEN_LEVEL_SELECT) render_level_select();
        return 1;
    }
    if (app.intro_active) {
        app.pd->system->resetElapsedTime();
        if (!effect_is_playing(LP_SOUND_LETS_GO)) {
            app.intro_active = 0; app.simulation_time = 0;
            play_effect(LP_SOUND_DOOR);
        }
        render_world(); return 1;
    }
    crank_change = app.pd->system->getCrankChange();
    if (crank_change < 0) crank_change = -crank_change;
    move_cursor(current);
    if ((current & (kButtonA | kButtonB)) == (kButtonA | kButtonB) &&
        (pushed & (kButtonA | kButtonB))) {
        app.pending_game_b = 0;
        app.game.paused = !app.game.paused;
        app.simulation_time = 0;
        app.pd->system->resetElapsedTime();
        play_effect(LP_SOUND_SELECT);
        render_world(); return 1;
    }
    if (app.pending_game_b) {
        app.pending_game_b = 0;
        app.menu_was_paused = app.game.paused;
        app.game.paused = 1;
        app.screen = SCREEN_ACTIONS;
        app.action_menu_item = app.selected_skill + 2; app.nuke_confirm = 0;
        app.release_rate_editing = 0;
        app.release_repeat_direction = 0; app.release_repeat_time = 0;
        app.action_animation_tick = 0; app.action_animation_time = 0;
        app.pd->system->resetElapsedTime();
        render_actions(); return 1;
    }
    /* Give A one update frame to join a B press before treating B as the
       standalone action-menu command. This prevents a one-frame menu flash
       when the player presses the A+B pause chord slightly unevenly. */
    if (pushed & kButtonB) app.pending_game_b = 1;
    if (pushed & kButtonA) {
        int selected = lp_game_select_at(&app.game, app.cursor_x, app.cursor_y);
        if (selected >= 0) lp_game_assign(&app.game, (unsigned)selected, (LPSkill)app.selected_skill);
    }
    elapsed = app.pd->system->getElapsedTime(); app.pd->system->resetElapsedTime();
    crank_time = crank_change / 360.0f;
    if (crank_time > elapsed * 3.0f) crank_time = elapsed * 3.0f;
    app.simulation_time += elapsed + crank_time;
    while (app.simulation_time >= (1.0f / LP_TICKS_PER_SECOND)) {
        uint16_t released_before = app.game.released;
        lp_game_tick(&app.game);
        if (!app.music_started && !released_before && app.game.released) {
            lp_adlib_play_music(&app.adlib, app.music_index);
            lp_adlib_pump(&app.adlib, LP_ADLIB_RING_TARGET);
            app.music_started = 1;
        }
        app.simulation_time -= (1.0f / LP_TICKS_PER_SECOND);
    }
    {
        LPSoundEvent sound;
        while (lp_game_take_sound(&app.game, &sound)) play_effect(sound);
    }
    if (lp_game_finished(&app.game)) {
        play_effect(lp_game_won(&app.game) ? LP_SOUND_SUCCESS : LP_SOUND_FAILURE);
        /* Completing a level in a test build must not count toward the natural
           progression, so the in-memory record is not touched either. */
        if (lp_game_won(&app.game) && !LP_TEST_BUILD) {
            app.save.completed[app.level_index / 8] |= (uint8_t)(1u << (app.level_index & 7));
            if ((unsigned)app.level_index % LP_LEVELS_PER_RATING < LP_LEVELS_PER_RATING - 1u)
                app.save.last_level = (uint8_t)(app.level_index + 1);
            else
                app.save.last_level = (uint8_t)app.level_index;
            save_progress();
        }
        set_reset_menu_visible(0);
        set_levels_menu_visible(0);
        app.screen = SCREEN_RESULT; render_result(); return 1;
    }
    render_world(); return 1;
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* pd, PDSystemEvent event, uint32_t arg) {
    (void)arg;
    if (event == kEventInit) {
        static const char* game_options[] = {"Continue", "Nuke"};
        const char* error = NULL;
        memset(&app, 0, sizeof app); app.pd = pd; app.cursor_speed = 1;
        app.detail_mode = 1; app.ground_mode = 1;
        load_progress();
        load_version();
        app.font = pd->graphics->loadFont("fonts/font-Cuberick-Bold.pft", &error);
        if (!app.font) pd->system->error("font Cuberick: %s", error);
        app.game_font = app.font;
        pd->graphics->setFont(app.font); pd->display->setRefreshRate(30);
        app.asset_file = pd->file->open("assets/lemmings.lpd", kFileRead);
        app.logo = pd->graphics->loadBitmap("SystemAssets/logo", &error);
        if (!app.asset_file || !app.logo || !load_adlib_image() ||
            !lp_pack_open(&app.pack, (LPReader){&app, file_read_at}) ||
            !lp_pack_load_sprites(&app.pack, &app.sprites) || !load_effects() ||
            !load_level((int)((app.save.last_level / LP_LEVELS_PER_RATING) *
                LP_LEVELS_PER_RATING + lp_progress_current(app.save.completed,
                app.save.last_level / LP_LEVELS_PER_RATING))))
            pd->system->error("Unable to load assets/lemmings.lpd: %s", pd->file->geterr());
        app.adlib_source = pd->sound->addSource(adlib_audio, &app.adlib, 0);
        if (!app.adlib_source) pd->system->error("Unable to create AdLib audio source");
        app.browser_rating = app.level_index / (int)LP_LEVELS_PER_RATING;
        app.browser_level = app.level_index % (int)LP_LEVELS_PER_RATING;
        app.screen = SCREEN_LOGO; pd->system->resetElapsedTime();
        app.game_menu = pd->system->addOptionsMenuItem(
            "Game", game_options, 2, menu_game, NULL);
        set_dither_menu_visible(1);
        pd->system->setUpdateCallback(update, &app);
    } else if (event == kEventTerminate && app.asset_file) {
        lp_adlib_stop(&app.adlib);
        if (app.adlib_source) {
            pd->sound->removeSource(app.adlib_source); app.adlib_source = NULL;
        }
        {
            int i;
            for (i = 0; i < EFFECT_VOICES; ++i) if (app.effect_players[i]) {
                pd->sound->sampleplayer->freePlayer(app.effect_players[i]);
                app.effect_players[i] = NULL;
            }
            for (i = 0; i < LP_SOUND_COUNT; ++i) if (app.effects[i]) {
                pd->sound->sample->freeSample(app.effects[i]); app.effects[i] = NULL;
            }
        }
        if (app.logo) { pd->graphics->freeBitmap(app.logo); app.logo = NULL; }
        pd->file->close(app.asset_file); app.asset_file = NULL;
    }
    return 0;
}
