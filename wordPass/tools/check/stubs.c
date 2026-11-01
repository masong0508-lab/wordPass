// Symbols board.c, dict.c and music.c need that live elsewhere in the ROM (gfx, main,
// settings, libnds). Keep this list in sync when a later item makes board.c or music.c
// reference a new gfx*/music*/sound* symbol: the native `make check` link will say so.
#include "game.h"

Settings S;
const u8 *dict_bin;
const u8 *pools_bin;
const u8 *variants_bin;

// music.c is compiled into the check itself, so its hooks (musicCascade ...) are real.

// libnds PSG API used by music.c
static int nextSoundId;
void soundEnable(void) {}
int  soundPlayPSG(DutyCycle cycle, u16 freq, u8 volume, u8 pan) { (void)cycle; (void)freq; (void)volume; (void)pan; return nextSoundId++; }
int  soundPlayNoise(u16 freq, u8 volume, u8 pan) { (void)freq; (void)volume; (void)pan; return nextSoundId++; }
void soundSetFreq(int soundId, u16 freq) { (void)soundId; (void)freq; }
void soundSetVolume(int soundId, u8 volume) { (void)soundId; (void)volume; }

// the LOADING screen (gfx.c) and the title's language switch (main.c)
void gfxLoading(const char *name) { (void)name; }
