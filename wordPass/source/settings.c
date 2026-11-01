// Player settings and records, saved to the card's FAT filesystem when one is
// available (flashcarts via DLDI, DSi SD). Without one they live for the session.
#include "game.h"
#include <fat.h>
#include <stdio.h>
#include <string.h>

Settings S;
static bool fatReady;
#define CFG_PATH "/wordPass.cfg"
#define CFG_MAGIC 0x57505331   // "WPS1"

void settingsDefaults(void)
{
    memset(&S, 0, sizeof(S));
    S.magic = CFG_MAGIC;
    S.theme = THEME_JEWEL;
    S.frame = FRAME_GOLD;
    S.accent = 0;           // auto per shelf
    S.hand = HAND_RIGHT;
    S.fire = FIRE_NORMAL;
    S.music = 1;
    S.library = LIB_FULL;
    S.spelling = SPELL_BOTH;
}

void settingsInit(void)
{
    settingsDefaults();
    fatReady = fatInitDefault();
    if (!fatReady) return;
    FILE *f = fopen(CFG_PATH, "rb");
    if (!f) return;
    Settings tmp;
    size_t n = fread(&tmp, 1, sizeof(tmp), f);
    fclose(f);
    if (n == sizeof(tmp) && tmp.magic == CFG_MAGIC) S = tmp;
}

bool settingsSave(void)
{
    if (!fatReady) return false;
    FILE *f = fopen(CFG_PATH, "wb");
    if (!f) return false;
    fwrite(&S, 1, sizeof(S), f);
    fclose(f);
    return true;
}

bool settingsPersistent(void) { return fatReady; }
