/* This file is part of Snes9x. See LICENSE file. */

#ifndef _PORT_H_
#define _PORT_H_

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

#ifdef RETRO_GO
#include <rg_system.h>
#endif

#ifndef INLINE
#define INLINE inline
#endif

/* Unaligned little-endian memory access.
 * The 65816 and SPC700 cores read 16 and 32-bit operands at arbitrary addresses, so plain pointer casts are not
 * safe: they are undefined behaviour in C and fault on CPUs that trap on misaligned accesses. memcpy compiles to
 * a single load/store where the CPU supports misaligned access (and to byte accesses with -mstrict-align), so
 * there is no cost compared to a pointer cast. These assume a little-endian host (see FAST_LSB_WORD_ACCESS).
 * Xtensa keeps the original pointer casts: it was not re-evaluated there, and memcpy may be slower on it. */
#ifdef __xtensa__
static INLINE uint16_t S9xLoad16(const void* p)
{
   return *(const uint16_t*) p;
}

static INLINE uint32_t S9xLoad32(const void* p)
{
   return *(const uint32_t*) p;
}

static INLINE void S9xStore16(void* p, uint16_t v)
{
   *(uint16_t*) p = v;
}

static INLINE void S9xStore32(void* p, uint32_t v)
{
   *(uint32_t*) p = v;
}
#else
static INLINE uint16_t S9xLoad16(const void* p)
{
   uint16_t v;
   memcpy(&v, p, sizeof(v));
   return v;
}

static INLINE uint32_t S9xLoad32(const void* p)
{
   uint32_t v;
   memcpy(&v, p, sizeof(v));
   return v;
}

static INLINE void S9xStore16(void* p, uint16_t v)
{
   memcpy(p, &v, sizeof(v));
}

static INLINE void S9xStore32(void* p, uint32_t v)
{
   memcpy(p, &v, sizeof(v));
}
#endif

#ifdef PSP
#define PIXEL_FORMAT BGR555
#else
#define PIXEL_FORMAT RGB565
#endif
/* The above is used to disable the 16-bit graphics mode checks sprinkled
 * throughout the code, if the pixel format is always 16-bit. */

#include "pixform.h"

#ifndef _WIN32

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef _MAX_DIR
#define _MAX_DIR   PATH_MAX
#endif

#ifndef _MAX_DRIVE
#define _MAX_DRIVE 1
#endif

#ifndef _MAX_FNAME
#define _MAX_FNAME PATH_MAX
#endif

#ifndef _MAX_EXT
#define _MAX_EXT   PATH_MAX
#endif

#ifndef _MAX_PATH
#define _MAX_PATH  PATH_MAX
#endif
#endif

#define SLASH_STR "/"
#define SLASH_CHAR '/'

#if defined(__i386__)  || defined(__i486__) || defined(__i586__) || defined(_XBOX1) || defined(__alpha__)
#define FAST_LSB_WORD_ACCESS
#elif defined(__MIPSEL__)
/* On little-endian MIPS, a 16-bit word can be read directly from an address
 * only if it's aligned. */
#define FAST_ALIGNED_LSB_WORD_ACCESS
#endif

#define ABS(X)   ((X) <  0  ? -(X) : (X))
#define MIN(A,B) ((A) < (B) ?  (A) : (B))
#define MAX(A,B) ((A) > (B) ?  (A) : (B))

/* Integer square root by Halleck's method, with Legalize's speedup */
static INLINE int32_t _isqrt(int32_t val)
{
   int32_t squaredbit, remainder, root;

   if (val < 1)
      return 0;

   squaredbit  = 1 << 30;
   remainder = val;
   root = 0;

   while (squaredbit > 0)
   {
      if (remainder >= (squaredbit | root))
      {
         remainder -= (squaredbit | root);
         root >>= 1;
         root |= squaredbit;
      }
      else
         root >>= 1;
      squaredbit >>= 2;
   }

   return root;
}
#endif
