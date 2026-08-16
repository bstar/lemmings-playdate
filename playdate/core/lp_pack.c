/*
 * Pack reader. See lp_pack.h for the format and ownership rules.
 *
 * Level planes arrive PackBits-compressed and are expanded through a bounded
 * streaming reader: a small fixed window pulled through the caller's LPReader,
 * never the whole compressed level at once. That keeps loading allocation-free
 * and gives the device the same code path the host tests exercise.
 */
#include "lp_pack.h"

#include <string.h>

enum { LP_HEADER_SIZE = 64, LP_RECORD_SIZE = 98, LP_OBJECT_SIZE = 20,
       LP_SPRITE_HEADER_SIZE = 16, LP_SPRITE_RECORD_SIZE = 14,
       LP_TOTAL_SPRITES = LP_SPRITE_SLOTS + LP_OBJECT_SPRITES,
       LP_PACKBITS_BUFFER_SIZE = 1024 };

typedef struct {
    const LPPack* pack;
    uint32_t offset, remaining;
    uint16_t at, count;
    uint8_t data[LP_PACKBITS_BUFFER_SIZE];
} LPPackBitsReader;

static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t i16(const uint8_t* p) { return (int16_t)u16(p); }
static uint32_t u32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int packbits_byte(LPPackBitsReader* input, uint8_t* value) {
    uint32_t amount;
    if (input->at == input->count) {
        if (!input->remaining) return 0;
        amount = input->remaining < sizeof input->data ? input->remaining : sizeof input->data;
        if (!input->pack->reader.read_at(input->pack->reader.context, input->offset,
                                         input->data, amount)) return 0;
        input->offset += amount; input->remaining -= amount;
        input->at = 0; input->count = (uint16_t)amount;
    }
    *value = input->data[input->at++];
    return 1;
}

static int packbits_decode(const LPPack* pack, uint32_t offset, uint32_t size,
                           uint8_t* output, uint32_t output_size) {
    LPPackBitsReader input;
    uint32_t written = 0;
    memset(&input, 0, sizeof input);
    input.pack = pack; input.offset = offset; input.remaining = size;
    while (written < output_size) {
        uint8_t control, value;
        uint32_t count, i;
        if (!packbits_byte(&input, &control)) return 0;
        if (control & 0x80) {
            count = (control & 0x7f) + 3u;
            if (count > output_size - written || !packbits_byte(&input, &value)) return 0;
            memset(output + written, value, count); written += count;
        } else {
            count = control + 1u;
            if (count > output_size - written) return 0;
            for (i = 0; i < count; ++i)
                if (!packbits_byte(&input, output + written++)) return 0;
        }
    }
    return !input.remaining && input.at == input.count;
}

int lp_pack_open(LPPack* pack, LPReader reader) {
    uint8_t header[LP_HEADER_SIZE];
    if (!pack || !reader.read_at || !reader.read_at(reader.context, 0, header, sizeof header)) return 0;
    if (memcmp(header, "LPD1", 4) != 0 || u16(header + 4) != 6 ||
        u16(header + 6) != LP_HEADER_SIZE || u32(header + 12) != LP_RECORD_SIZE) return 0;
    memset(pack, 0, sizeof *pack);
    pack->reader = reader;
    pack->level_count = u32(header + 8);
    pack->record_size = u32(header + 12);
    pack->directory_offset = u32(header + 16);
    pack->file_size = u32(header + 20);
    pack->sprite_offset = u32(header + 24); pack->sprite_size = u32(header + 28);
    memcpy(pack->source_sha256, header + 32, 32);
    return pack->level_count == 120;
}

int lp_pack_load_sprites(const LPPack* pack, LPSpriteAtlas* output) {
    uint8_t header[LP_SPRITE_HEADER_SIZE];
    uint8_t records[LP_TOTAL_SPRITES * LP_SPRITE_RECORD_SIZE];
    uint32_t table_size, payload_offset;
    unsigned i;
    if (!pack || !output || pack->sprite_size > LP_SPRITE_ATLAS_MAX ||
        pack->sprite_offset + pack->sprite_size > pack->file_size) return 0;
    if (!pack->reader.read_at(pack->reader.context, pack->sprite_offset, header, sizeof header) ||
        memcmp(header, "LPS3", 4) || u16(header + 4) != LP_SPRITE_SLOTS ||
        u16(header + 6) != LP_OBJECT_SPRITES || u16(header + 8) != LP_SPRITE_RECORD_SIZE ||
        u32(header + 12) != pack->sprite_size) return 0;
    table_size = LP_TOTAL_SPRITES * LP_SPRITE_RECORD_SIZE;
    payload_offset = LP_SPRITE_HEADER_SIZE + table_size;
    if (!pack->reader.read_at(pack->reader.context, pack->sprite_offset + LP_SPRITE_HEADER_SIZE,
                              records, table_size) ||
        !pack->reader.read_at(pack->reader.context, pack->sprite_offset + payload_offset,
                              output->data, pack->sprite_size - payload_offset)) return 0;
    output->data_size = pack->sprite_size - payload_offset;
    if (output->data_size < LP_ACTION_BYTES + LP_ACTION_DIGIT_BYTES +
                            LP_EXPLOSION_PARTICLE_BYTES + LP_TERRAIN_MASK_BYTES ||
        u16(header + 10) != LP_EXPLOSION_FRAMES) return 0;
    output->terrain_masks = output->data + output->data_size - LP_TERRAIN_MASK_BYTES;
    output->explosion_particles = output->terrain_masks - LP_EXPLOSION_PARTICLE_BYTES;
    output->action_digits = output->explosion_particles - LP_ACTION_DIGIT_BYTES;
    output->action_tiles = output->action_digits - LP_ACTION_BYTES;
    for (i = 0; i < LP_TOTAL_SPRITES; ++i) {
        const uint8_t* p = records + i * LP_SPRITE_RECORD_SIZE;
        LPSprite* sprite = i < LP_SPRITE_SLOTS ? &output->sprites[i] :
                           &output->object_sprites[i - LP_SPRITE_SLOTS];
        uint32_t absolute = u32(p);
        memset(sprite, 0, sizeof *sprite);
        if (!absolute) continue;
        if (absolute < payload_offset || absolute >= pack->sprite_size) return 0;
        sprite->data_offset = absolute - payload_offset;
        sprite->width = u16(p + 4); sprite->height = u16(p + 6);
        sprite->frame_count = p[8]; sprite->row_bytes = p[9];
        sprite->offset_x = (int8_t)p[10]; sprite->offset_y = (int8_t)p[11];
        sprite->frame_bytes = u16(p + 12);
        if (sprite->data_offset + sprite->frame_count * sprite->frame_bytes > output->data_size) return 0;
    }
    return 1;
}

int lp_pack_load_level(const LPPack* pack, uint32_t index, LPLevelAssets* output) {
    uint8_t record[LP_RECORD_SIZE];
    uint8_t objects[LP_MAX_OBJECTS * LP_OBJECT_SIZE];
    uint32_t visual_offset, bayer2_offset, cluster2_offset, solid_offset;
    uint32_t steel_offset, object_offset, plane_size;
    uint32_t plane_offsets[5], plane_sizes[5];
    uint16_t object_count, steel_size;
    unsigned i;
    if (!pack || !output || !output->visual || !output->visual_bayer2 ||
        !output->visual_cluster2 || !output->solid || !output->steel ||
        index >= pack->level_count) return 0;
    if (!pack->reader.read_at(pack->reader.context,
            pack->directory_offset + index * pack->record_size, record, sizeof record)) return 0;
    memset(&output->meta, 0, sizeof output->meta);
    output->meta.rating = record[0]; output->meta.number = record[1];
    output->meta.style = record[2]; output->meta.special = record[3];
    output->meta.source_id = u16(record + 4); output->meta.odd_record = i16(record + 6);
    output->meta.release_rate = u16(record + 8); output->meta.lemming_count = u16(record + 10);
    output->meta.rescue_count = u16(record + 12); output->meta.time_minutes = u16(record + 14);
    for (i = 0; i < 8; ++i) output->meta.skills[i] = u16(record + 16 + i * 2);
    output->meta.camera_x = u16(record + 32);
    memcpy(output->meta.name, record + 34, 32); output->meta.name[32] = '\0';
    for (i = 32; i && output->meta.name[i - 1] == ' '; --i) output->meta.name[i - 1] = '\0';
    visual_offset = u32(record + 66); bayer2_offset = u32(record + 70);
    cluster2_offset = u32(record + 74); solid_offset = u32(record + 78);
    steel_offset = u32(record + 82); object_offset = u32(record + 86);
    plane_size = u32(record + 90); object_count = u16(record + 94);
    steel_size = u16(record + 96);
    plane_offsets[0] = visual_offset; plane_offsets[1] = bayer2_offset;
    plane_offsets[2] = cluster2_offset; plane_offsets[3] = solid_offset;
    plane_offsets[4] = steel_offset;
    for (i = 0; i < 4; ++i) {
        if (plane_offsets[i] >= plane_offsets[i + 1]) return 0;
        plane_sizes[i] = plane_offsets[i + 1] - plane_offsets[i];
    }
    plane_sizes[4] = steel_size;
    if (plane_size != LP_PLANE_BYTES || !steel_size || object_count > LP_MAX_OBJECTS ||
        steel_offset > pack->sprite_offset || steel_size > pack->sprite_offset - steel_offset ||
        object_offset + object_count * LP_OBJECT_SIZE > pack->sprite_offset) return 0;
    if (!packbits_decode(pack, visual_offset, plane_sizes[0], output->visual, LP_PLANE_BYTES) ||
        !packbits_decode(pack, bayer2_offset, plane_sizes[1], output->visual_bayer2, LP_PLANE_BYTES) ||
        !packbits_decode(pack, cluster2_offset, plane_sizes[2], output->visual_cluster2, LP_PLANE_BYTES) ||
        !packbits_decode(pack, solid_offset, plane_sizes[3], output->solid, LP_PLANE_BYTES) ||
        !packbits_decode(pack, steel_offset, plane_sizes[4], output->steel, LP_PLANE_BYTES) ||
        (object_count && !pack->reader.read_at(pack->reader.context, object_offset, objects,
                                               object_count * LP_OBJECT_SIZE))) return 0;
    output->meta.object_count = object_count;
    for (i = 0; i < object_count; ++i) {
        const uint8_t* p = objects + i * LP_OBJECT_SIZE;
        LPObject* object = &output->objects[i];
        object->x = i16(p); object->y = i16(p + 2);
        object->object_id = p[4]; object->flags = p[5];
        object->trigger_effect = p[6]; object->sound_effect = p[7];
        object->trigger_x1 = i16(p + 8); object->trigger_y1 = i16(p + 10);
        object->trigger_x2 = i16(p + 12); object->trigger_y2 = i16(p + 14);
        object->anim_flags = u16(p + 16); object->start_frame = p[18];
        object->frame_count = p[19];
    }
    return 1;
}
