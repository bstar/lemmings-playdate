#ifndef LP_DBOPL_H
#define LP_DBOPL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* lp_dbopl_create(uint32_t sample_rate);
void lp_dbopl_destroy(void* context);
void lp_dbopl_reset(void* context, uint32_t sample_rate);
void lp_dbopl_write(void* context, uint8_t reg, uint8_t value);
void lp_dbopl_generate(void* context, int32_t* mono, int frames);

#ifdef __cplusplus
}
#endif

#endif
