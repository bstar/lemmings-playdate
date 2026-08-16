#ifndef LP_PACK_H
#define LP_PACK_H

#include <stddef.h>
#include <stdint.h>

#define LP_LEVEL_WIDTH 1600
#define LP_LEVEL_HEIGHT 160
#define LP_ROW_BYTES 200
#define LP_PLANE_BYTES 32000
#define LP_MAX_OBJECTS 32
#define LP_SPRITE_SLOTS 40
#define LP_OBJECT_SPRITES 80
#define LP_SPRITE_ATLAS_MAX 262144
#define LP_TERRAIN_MASK_BYTES 388
#define LP_EXPLOSION_FRAMES 51
#define LP_EXPLOSION_PARTICLES 80
#define LP_EXPLOSION_PARTICLE_BYTES (LP_EXPLOSION_FRAMES * LP_EXPLOSION_PARTICLES * 2)
#define LP_ACTION_COUNT 12
#define LP_ACTION_WIDTH 16
#define LP_ACTION_HEIGHT 40
#define LP_ACTION_ROW_BYTES 2
#define LP_ACTION_BYTES (LP_ACTION_COUNT * LP_ACTION_ROW_BYTES * LP_ACTION_HEIGHT)
#define LP_ACTION_DIGIT_COUNT 20
#define LP_ACTION_DIGIT_BYTES (LP_ACTION_DIGIT_COUNT * 8)

typedef int (*LPReadAt)(void* context, uint32_t offset, void* output, size_t size);

typedef struct {
    void* context;
    LPReadAt read_at;
} LPReader;

typedef struct {
    int16_t x, y;
    uint8_t object_id, flags, trigger_effect, sound_effect;
    int16_t trigger_x1, trigger_y1, trigger_x2, trigger_y2;
    uint16_t anim_flags;
    uint8_t start_frame, frame_count;
} LPObject;

typedef struct {
    uint8_t rating, number, style, special;
    uint16_t source_id;
    int16_t odd_record;
    uint16_t release_rate, lemming_count, rescue_count, time_minutes;
    uint16_t skills[8];
    uint16_t camera_x;
    char name[33];
    uint16_t object_count;
} LPLevelMeta;

typedef struct {
    uint32_t level_count, record_size, directory_offset, file_size;
    uint32_t sprite_offset, sprite_size;
    uint8_t source_sha256[32];
    LPReader reader;
} LPPack;

typedef struct {
    LPLevelMeta meta;
    uint8_t* visual;
    uint8_t* visual_bayer2;
    uint8_t* visual_cluster2;
    uint8_t* solid;
    uint8_t* steel;
    LPObject objects[LP_MAX_OBJECTS];
    const uint8_t* terrain_masks;
} LPLevelAssets;

typedef struct {
    uint32_t data_offset;
    uint16_t width, height;
    uint8_t frame_count, row_bytes;
    int8_t offset_x, offset_y;
    uint16_t frame_bytes;
} LPSprite;

typedef struct {
    LPSprite sprites[LP_SPRITE_SLOTS];
    LPSprite object_sprites[LP_OBJECT_SPRITES];
    uint8_t data[LP_SPRITE_ATLAS_MAX];
    uint32_t data_size;
    const uint8_t* explosion_particles;
    const uint8_t* action_tiles;
    const uint8_t* action_digits;
    const uint8_t* terrain_masks;
} LPSpriteAtlas;

int lp_pack_open(LPPack* pack, LPReader reader);
int lp_pack_load_level(const LPPack* pack, uint32_t index, LPLevelAssets* output);
int lp_pack_load_sprites(const LPPack* pack, LPSpriteAtlas* output);

#endif
