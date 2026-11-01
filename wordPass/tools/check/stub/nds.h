// Stand-in for libnds <nds.h> so board.c, dict.c and music.c compile natively for the
// rule tests (tools/check). Only what source/ touches: fixed-width types, ARGB16, the
// placement attributes (empty) and the PSG calls music.c makes (stubbed in stubs.c).
#ifndef CHECK_STUB_NDS_H
#define CHECK_STUB_NDS_H
#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

#define ARGB16(a, r, g, b) ((u16)(((a) << 15) | ((b) << 10) | ((g) << 5) | (r)))
#define DTCM_BSS
#define ITCM_CODE
#define ALIGN(n)

// PSG API used by music.c
typedef enum {
    DutyCycle_0 = 0, DutyCycle_12, DutyCycle_25, DutyCycle_37,
    DutyCycle_50, DutyCycle_62, DutyCycle_75, DutyCycle_87
} DutyCycle;
void soundEnable(void);
int  soundPlayPSG(DutyCycle cycle, u16 freq, u8 volume, u8 pan);
int  soundPlayNoise(u16 freq, u8 volume, u8 pan);
void soundSetFreq(int soundId, u16 freq);
void soundSetVolume(int soundId, u8 volume);

#endif
