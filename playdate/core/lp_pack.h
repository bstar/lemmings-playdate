/*
 * lp_pack: reader for the LPD asset pack.
 *
 * tools/assetc converts a DOS Lemmings installation into a single pack file.
 * This module reads it back at runtime. Nothing here understands gameplay; it
 * turns pack bytes into the planes, objects, and sprites that lp_game and the
 * platform adapter consume.
 *
 * All I/O goes through an LPReader callback rather than stdio, so the same
 * loader serves the host test build, the Simulator, and the device, where file
 * access is an SDK call. Level planes are decompressed through a bounded
 * streaming reader, so loading never allocates and never needs the whole
 * compressed level in memory.
 *
 * The caller owns all plane memory. Load functions fill buffers that the
 * caller supplies and keeps alive; this module allocates nothing.
 */
#ifndef LP_PACK_H
#define LP_PACK_H

#include <stddef.h>
#include <stdint.h>

/* Terrain is a 1600 x 160 one-bit-per-pixel field, so a row is 200 bytes and
   a full plane is 32,000. Every plane in LPLevelAssets uses this geometry. */
#define LP_LEVEL_WIDTH 1600
#define LP_LEVEL_HEIGHT 160
#define LP_ROW_BYTES 200
#define LP_PLANE_BYTES 32000

#define LP_MAX_OBJECTS 32

/* Lemming animations occupy the first slots; object animations, indexed as
   style * 16 + object_id, occupy their own table. */
#define LP_SPRITE_SLOTS 40
#define LP_OBJECT_SPRITES 80
#define LP_SPRITE_ATLAS_MAX 262144

/* Bitmasks the skills cut out of terrain, shared by basher, miner, and the
   explosion. */
#define LP_TERRAIN_MASK_BYTES 388

/* The exact DOS explosion table: 51 frames of 80 particles, each a signed
   x,y byte pair. */
#define LP_EXPLOSION_FRAMES 51
#define LP_EXPLOSION_PARTICLES 80
#define LP_EXPLOSION_PARTICLE_BYTES (LP_EXPLOSION_FRAMES * LP_EXPLOSION_PARTICLES * 2)

/* Action-panel artwork: 12 tiles of 16 x 40, plus a small digit strip. */
#define LP_ACTION_COUNT 12
#define LP_ACTION_WIDTH 16
#define LP_ACTION_HEIGHT 40
#define LP_ACTION_ROW_BYTES 2
#define LP_ACTION_BYTES (LP_ACTION_COUNT * LP_ACTION_ROW_BYTES * LP_ACTION_HEIGHT)
#define LP_ACTION_DIGIT_COUNT 20
#define LP_ACTION_DIGIT_BYTES (LP_ACTION_DIGIT_COUNT * 8)

/* Read `size` bytes at `offset` into `output`. Return nonzero on success.
   Implemented by the caller over whatever file API the platform provides. */
typedef int (*LPReadAt)(void* context, uint32_t offset, void* output, size_t size);

typedef struct {
    void* context;
    LPReadAt read_at;
} LPReader;

/* A placed level object: entrance, exit, trap, water, or one-way wall.
 *
 * `object_id` selects artwork within the level's style. `trigger_effect`
 * selects behaviour, which lp_game interprets: 1 exit, 4 and 6 traps, 5 water,
 * 7 and 8 one-way walls. The trigger rectangle is the area that fires it, and
 * is independent of where the artwork is drawn.
 */
typedef struct {
    int16_t x, y;
    uint8_t object_id, flags, trigger_effect, sound_effect;
    int16_t trigger_x1, trigger_y1, trigger_x2, trigger_y2;
    uint16_t anim_flags;
    uint8_t start_frame, frame_count;
} LPObject;

/* Level parameters as authored in the DOS data.
 *
 * `rating` is 0..3 for Fun, Tricky, Taxing, Mayhem and `number` is the level
 * within it. `style` selects the tile set. `skills` is indexed by LPSkill.
 * `camera_x` is the DOS start scroll, which assumes a 320-pixel viewport and
 * so does not by itself frame the entrance on a narrower display.
 */
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

/* An opened pack. `source_sha256` digests the DOS files it was built from, so
   a build can prove which installation produced its assets. */
typedef struct {
    uint32_t level_count, record_size, directory_offset, file_size;
    uint32_t sprite_offset, sprite_size;
    uint8_t source_sha256[32];
    LPReader reader;
} LPPack;

/* One loaded level. The caller points the five plane pointers at buffers of
 * LP_PLANE_BYTES each before calling lp_pack_load_level.
 *
 * `visual` is what gets drawn and `solid` is what gameplay collides against;
 * they differ because terrain edits must update both, and because dithering
 * changes appearance without changing collision. `visual_bayer2` and
 * `visual_cluster2` are alternative dither renderings selectable at runtime,
 * and may be left NULL if the caller does not offer that choice. `steel`
 * marks terrain that skills cannot remove.
 */
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

/* One animation in the atlas. `offset_x` and `offset_y` position the frame
   against the lemming's or object's anchor point; frames are one bit per
   pixel, `row_bytes` per row and `frame_bytes` apart. */
typedef struct {
    uint32_t data_offset;
    uint16_t width, height;
    uint8_t frame_count, row_bytes;
    int8_t offset_x, offset_y;
    uint16_t frame_bytes;
} LPSprite;

/* Every sprite in one allocation, loaded once and reused across levels.
   Identical encoded frames are shared, so the atlas is smaller than the sum
   of its animations. The trailing pointers alias into `data`. */
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

/* Read the pack header and directory. Returns nonzero on success. The reader
   is stored and must stay valid for as long as the pack is used. */
int lp_pack_open(LPPack* pack, LPReader reader);

/* Decompress level `index` into `output`, whose plane pointers the caller has
   already set. Returns nonzero on success. */
int lp_pack_load_level(const LPPack* pack, uint32_t index, LPLevelAssets* output);

/* Load the shared sprite atlas. Call once; the result is level-independent. */
int lp_pack_load_sprites(const LPPack* pack, LPSpriteAtlas* output);

#endif
