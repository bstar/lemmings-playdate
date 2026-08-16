#ifndef LYNX_ACTION_ART_H
#define LYNX_ACTION_ART_H

#include <stdint.h>

/* Undithered native crops from the supplied 160x102 Lynx action panel. */
static const uint32_t lynx_release_rows[32] = {
    0, 0, 0, 0,
    0, 0, 0, 0x00040000u,
    0x0007e000u, 0x0007e000u, 0x0827e000u, 0x0ffffff0u,
    0x0ffffff0u, 0x0007e000u, 0x0007f000u, 0x0007e000u,
    0x0007e000u, 0, 0, 0,
    0, 0, 0x10000000u, 0x1ffffff0u,
    0x1ffffff0u, 0, 0, 0,
    0, 0, 0, 0,
};

static const uint32_t lynx_nuke_rows[32] = {
    0, 0, 0x00000cc0u, 0x00001620u,
    0x003e2f20u, 0x00ffdfd0u, 0x03fffffcu, 0x0783ffe8u,
    0x071ffe58u, 0x0e7ffc7cu, 0x1cfffc38u, 0x19fffc00u,
    0x19fffe00u, 0x3bfffe00u, 0x3bfffe00u, 0x3bfffe00u,
    0x1ffffe00u, 0x1ffffc00u, 0x0ffffc00u, 0x0fffec00u,
    0x0fffd800u, 0x07ffb000u, 0x03fc6000u, 0x01ffc000u,
    0x007e0000u, 0, 0, 0,
    0, 0, 0, 0,
};

#endif
