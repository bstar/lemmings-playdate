#include "lp_dbopl.h"

#include "../../third_party/dbopl/dbopl.h"

#include <new>
#include <stdlib.h>

extern "C" void* lp_dbopl_create(uint32_t sample_rate) {
    void* memory = malloc(sizeof(DBOPL::Chip));
    DBOPL::Chip* chip;
    if (!memory) return 0;
    chip = new (memory) DBOPL::Chip();
    DBOPL::InitTables();
    chip->Setup(sample_rate);
    return chip;
}

extern "C" void lp_dbopl_destroy(void* context) {
    DBOPL::Chip* chip = static_cast<DBOPL::Chip*>(context);
    if (!chip) return;
    chip->~Chip();
    free(chip);
}

extern "C" void lp_dbopl_reset(void* context, uint32_t sample_rate) {
    DBOPL::Chip* chip = static_cast<DBOPL::Chip*>(context);
    if (!chip) return;
    chip->~Chip();
    chip = new (chip) DBOPL::Chip();
    DBOPL::InitTables();
    chip->Setup(sample_rate);
}

extern "C" void lp_dbopl_write(void* context, uint8_t reg, uint8_t value) {
    static_cast<DBOPL::Chip*>(context)->WriteReg(reg, value);
}

extern "C" void lp_dbopl_generate(void* context, int32_t* mono, int frames) {
    static_cast<DBOPL::Chip*>(context)->GenerateBlock2((Bitu)frames, mono);
}
