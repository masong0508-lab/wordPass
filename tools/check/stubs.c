// Symbols board.c, dict.c and music.c need that live elsewhere in the ROM (gfx, main,
// settings, libnds). Keep this list in sync when a later item makes board.c or music.c
// reference a new gfx*/music*/sound* symbol: the native `make check` link will say so.
#include "game.h"

Settings S;
const u8 *dict_bin;
const u8 *pools_bin;
const u8 *variants_bin;

// music.c is compiled into the check itself, so its hooks (musicCascade ...) are real.

// libnds PSG API used by music.c. Every channel's last values are kept so board_test.c can read what the
// sequencer sent (stubHz/stubVol/stubDuty/stubPan by sound id, stubWrites counts every hardware write).
int stubHz[32], stubVol[32], stubDuty[32], stubPan[32], stubWrites, stubRedundant;   // stubRedundant: writes that changed nothing
static int nextSoundId;
void soundEnable(void) {}
int  soundPlayPSG(DutyCycle cycle, u16 freq, u8 volume, u8 pan)
{
    int id = nextSoundId++; stubHz[id] = freq; stubVol[id] = volume; stubDuty[id] = (int)cycle; stubPan[id] = pan; return id;
}
int  soundPlayNoise(u16 freq, u8 volume, u8 pan)
{
    int id = nextSoundId++; stubHz[id] = freq; stubVol[id] = volume; stubDuty[id] = -1; stubPan[id] = pan; return id;
}
void soundSetFreq(int soundId, u16 freq) { if (stubHz[soundId] == freq) stubRedundant++; stubHz[soundId] = freq; stubWrites++; }
void soundSetVolume(int soundId, u8 volume) { if (stubVol[soundId] == volume) stubRedundant++; stubVol[soundId] = volume; stubWrites++; }
void soundSetWaveDuty(int soundId, DutyCycle cycle) { if (stubDuty[soundId] == (int)cycle) stubRedundant++; stubDuty[soundId] = (int)cycle; stubWrites++; }
void soundSetPan(int soundId, u8 pan) { if (stubPan[soundId] == pan) stubRedundant++; stubPan[soundId] = pan; stubWrites++; }
// the LOADING screen (gfx.c) and the title's language switch (main.c)
void gfxLoading(const char *name) { (void)name; }
