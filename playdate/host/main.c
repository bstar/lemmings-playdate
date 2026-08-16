#include "lp_game.h"
#include "lp_pack.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct { FILE* file; } HostFile;

static int read_at(void* context, uint32_t offset, void* output, size_t size) {
    HostFile* host = context;
    return fseek(host->file, (long)offset, SEEK_SET) == 0 && fread(output, 1, size, host->file) == size;
}

int main(int argc, char** argv) {
    HostFile source;
    LPPack pack;
    LPLevelAssets level;
    LPGame game;
    LPSpriteAtlas sprites;
    uint8_t visual[LP_PLANE_BYTES], visual_bayer2[LP_PLANE_BYTES];
    uint8_t visual_cluster2[LP_PLANE_BYTES], solid[LP_PLANE_BYTES], steel[LP_PLANE_BYTES];
    int ticks = 300;
    if (argc < 2) { fprintf(stderr, "usage: %s PACK [LEVEL]\n", argv[0]); return 2; }
    source.file = fopen(argv[1], "rb");
    if (!source.file || !lp_pack_open(&pack, (LPReader){&source, read_at})) { fprintf(stderr, "invalid pack\n"); return 1; }
    level.visual = visual; level.visual_bayer2 = visual_bayer2;
    level.visual_cluster2 = visual_cluster2; level.solid = solid; level.steel = steel;
    if (!lp_pack_load_sprites(&pack, &sprites)) { fprintf(stderr, "sprite atlas load failed\n"); return 1; }
    level.terrain_masks = sprites.terrain_masks;
    if (argc <= 2) {
        uint32_t index;
        for (index = 0; index < pack.level_count; ++index) {
            int smoke_ticks = 300;
            if (!lp_pack_load_level(&pack, index, &level)) {
                fprintf(stderr, "level %u load failed\n", index); return 1;
            }
            lp_game_init(&game, &level);
            while (smoke_ticks--) lp_game_tick(&game);
            if (game.released > level.meta.lemming_count || game.alive > game.released) {
                fprintf(stderr, "level %u simulation invariant failed\n", index); return 1;
            }
        }
        printf("loaded and simulated all %u levels\n", pack.level_count);
    }
    if (!lp_pack_load_level(&pack, argc > 2 ? (uint32_t)atoi(argv[2]) : 0, &level)) { fprintf(stderr, "level load failed\n"); return 1; }
    lp_game_init(&game, &level);
    while (ticks--) lp_game_tick(&game);
    printf("%s: tick=%u released=%u alive=%u rescued=%u camera=%u\n",
           level.meta.name, game.tick, game.released, game.alive, game.rescued, level.meta.camera_x);
    fclose(source.file);
    return 0;
}
