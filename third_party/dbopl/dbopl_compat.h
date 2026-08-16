/* Minimal DOSBox type compatibility for the standalone DBOPL core. */
#ifndef LP_DBOPL_COMPAT_H
#define LP_DBOPL_COMPAT_H

#include <stdint.h>

typedef uint8_t Bit8u;
typedef int8_t Bit8s;
typedef uint16_t Bit16u;
typedef int16_t Bit16s;
typedef uint32_t Bit32u;
typedef int32_t Bit32s;
typedef uintptr_t Bitu;
typedef intptr_t Bits;

#define DB_FASTCALL
#define INLINE inline
#if defined(__GNUC__) || defined(__clang__)
#define GCC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define GCC_UNLIKELY(x) (x)
#endif

#endif
