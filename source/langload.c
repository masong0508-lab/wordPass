// Languages, loading part: the ROM filesystem (NitroFS) holds lang/<code>/{dict,pools,letters}.bin for every
// language but English, which is compiled in so a card or emulator without a working filesystem still plays.
// Loading a language frees the previous one's buffers, shows the LOADING screen, reads the three files into
// heap buffers (the largest dictionary is 1.6 MB of the 4 MB main RAM; the ROM's own data is the rest) and
// hands them to langApply. The native tests build this file too and read data/lang directly.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef ARM9
#include <filesystem.h>
#define LANG_ROOT "nitro:/lang/"
#else
#include <time.h>
#define LANG_ROOT "data/lang/"
#endif

static bool fsReady;
static bool avail[LANG_COUNT];
static u8 *bufDict, *bufPools, *bufLetters;
static int lastMs;

static void path(char *out, int idx, const char *file)
{
    strcpy(out, LANG_ROOT); strcat(out, langCode(idx)); strcat(out, "/"); strcat(out, file);
}

#ifdef ARM9
// ---- the fast path. NitroFS in cartridge mode (a retail card, DeSmuME, melonDS: no FAT to read the .nds from) reads every 512 byte
// page with CARD_CLK_SLOW and the longest command delay, about 2 ms a page, which measured 4.1 seconds per megabyte in
// DeSmuME: 5 to 7 seconds for a language. So when the ROM is read from the card itself, the three files are located
// through the ROM's own file tables (the same FNT and FAT NitroFS reads) and read with the header's card control word,
// the way the card boot code reads. When the program was started from a flashcart (a path in argv and a working FAT), NitroFS
// reads the .nds from the SD card through FAT, which is already fast, and the raw path is never used. The raw path proves
// itself first (langInit compares what it reads with what NitroFS reads); anything that does not match falls back.
static bool rawOk;
static u8 *fnt;
static u32 rawPage[128];

static void rawPageRead(u32 off)                          // 512 bytes at a 512 byte aligned ROM offset
{
    cardParamCommand(CARD_CMD_DATA_READ, off, (__NDSHeader->cardControl13 & ~CARD_BLK_SIZE(7)) | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1), rawPage, 128);
}

static void rawRead(u32 off, u8 *dst, u32 len)
{
    while (len) {
        u32 pg = off & ~511u, in = off - pg, n = 512 - in;
        if (n > len) n = len;
        if (in == 0 && n == 512 && ((u32)dst & 3) == 0) cardParamCommand(CARD_CMD_DATA_READ, pg, (__NDSHeader->cardControl13 & ~CARD_BLK_SIZE(7)) | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1), (u32 *)dst, 128);
        else { rawPageRead(pg); memcpy(dst, (u8 *)rawPage + in, n); }
        off += n; dst += n; len -= n;
    }
}

static u32 rd16(const u8 *b) { return (u32)b[0] | ((u32)b[1] << 8); }
static u32 rd32(const u8 *b) { return rd16(b) | (rd16(b + 2) << 16); }

// ROM offset and size of "lang/xx/file.bin" through the FNT (directories, then names) and the FAT
static bool rawLocate(const char *path, u32 *start, u32 *size)
{
    u32 dir = 0xF000;
    const char *p = path;
    for (;;) {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        const u8 *ent = fnt + rd32(fnt + 8 * (dir & 0xFFF));
        u32 fid = rd16(fnt + 8 * (dir & 0xFFF) + 4);
        bool found = false;
        while (*ent) {
            size_t len = *ent & 0x7F;
            bool isDir = (*ent & 0x80) != 0;
            const u8 *nm = ent + 1;
            ent += 1 + len;
            u32 did = 0;
            if (isDir) { did = rd16(ent); ent += 2; }
            if (len == n && strncasecmp((const char *)nm, p, n) == 0) {
                if (e && isDir) { dir = did; found = true; break; }
                if (!e && !isDir) {
                    u8 fe[8];
                    rawRead(__NDSHeader->fatOffset + 8 * fid, fe, 8);
                    *start = rd32(fe); *size = rd32(fe + 4) - *start;
                    return true;
                }
                return false;
            }
            if (!isDir) fid++;
        }
        if (!found) return false;
        p = e + 1;
    }
}

static void rawInit(void)
{
    rawOk = false;
    const struct __argv *a = __system_argv;
    if (a->argvMagic == ARGV_MAGIC && a->argc > 0 && settingsPersistent()) return;   // started from a flashcart's SD card (FAT works): NitroFS reads the .nds through FAT
    u32 fs = __NDSHeader->filenameSize;
    if (!__NDSHeader->cardControl13 || fs < 8 || fs > 16384) return;
    fnt = (u8 *)malloc(fs + 4);
    if (!fnt) return;
    memset(fnt, 0, fs + 4);
    rawRead(__NDSHeader->filenameOffset, fnt, fs);
    // proof: the raw path must find the same file at the same size NitroFS does, with the same first bytes
    u32 st, sz;
    int fd = open("nitro:/lang/es/letters.bin", O_RDONLY);
    if (fd < 0) { free(fnt); fnt = NULL; return; }
    off_t nsz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    u8 a4[4], b4[4];
    bool ok = read(fd, a4, 4) == 4;
    close(fd);
    ok = ok && rawLocate("lang/es/letters.bin", &st, &sz) && (off_t)sz == nsz;
    if (ok) { rawRead(st, b4, 4); ok = memcmp(a4, b4, 4) == 0 && memcmp(b4, "WPL1", 4) == 0; }
    if (!ok) { free(fnt); fnt = NULL; return; }
    rawOk = true;
}

static u8 *rawSlurp(int idx, const char *file, size_t *n)
{
    char rel[40];
    strcpy(rel, "lang/"); strcat(rel, langCode(idx)); strcat(rel, "/"); strcat(rel, file);
    u32 st, sz;
    if (!rawLocate(rel, &st, &sz) || sz == 0) return NULL;
    u8 *b = (u8 *)malloc(sz + 4);
    if (!b) return NULL;
    rawRead(st, b, sz);
    *n = sz;
    return b;
}
#endif

// One read() of the whole file into the heap: a stdio fread would go through 1 KB buffer refills, one card
// command each, and a language is a megabyte and a half.
static u8 *slurp(const char *p, size_t *n)
{
    int fd = open(p, O_RDONLY);
    if (fd < 0) return NULL;
    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    u8 *b = sz > 0 ? (u8 *)malloc((size_t)sz) : NULL;
    if (b) {
        size_t got = 0;
        while (got < (size_t)sz) {                       // a device may return less than asked
            ssize_t k = read(fd, b + got, (size_t)sz - got);
            if (k <= 0) break;
            got += (size_t)k;
        }
        if (got != (size_t)sz) { free(b); b = NULL; }
    }
    close(fd);
    if (b && n) *n = (size_t)sz;
    return b;
}

static void freeBuffers(void)
{
    free(bufDict); free(bufPools); free(bufLetters);
    bufDict = bufPools = bufLetters = NULL;
}

void langInit(void)
{
    langInitEnglish();
    memset(avail, 0, sizeof(avail));
    avail[LANG_EN] = true;
#ifdef ARM9
    fsReady = nitroFSInit(NULL);
    if (fsReady) rawInit();
#else
    fsReady = true;
#endif
    if (!fsReady) return;
    for (int i = 1; i < LANG_COUNT; i++) {
#ifdef ARM9
        if (rawOk) {                                  // the card path knows the file table already: no NitroFS lookups
            char rel[40]; u32 st, sz;
            strcpy(rel, "lang/"); strcat(rel, langCode(i)); strcat(rel, "/letters.bin");
            avail[i] = rawLocate(rel, &st, &sz) && sz == LANG_LETTERS_BYTES;
            continue;
        }
#endif
        char p[48];
        path(p, i, "letters.bin");
        int fd = open(p, O_RDONLY);
        if (fd >= 0) { avail[i] = true; close(fd); }
    }
}

bool langAvailable(int idx) { return idx >= 0 && idx < LANG_COUNT && avail[idx]; }
int  langLoadMs(void) { return lastMs; }
#ifdef ARM9
bool langFastPath(void) { return rawOk; }
#else
bool langFastPath(void) { return false; }
#endif

// one of a language's files into a heap buffer: the raw card path when it has proved itself, else NitroFS
#ifdef ARM9
#define LOAD(idx, file, buf, size) do { buf = rawOk ? rawSlurp(idx, file, &size) : NULL; if (!buf) { char lp[48]; path(lp, idx, file); buf = slurp(lp, &size); } } while (0)
#else
#define LOAD(idx, file, buf, size) do { char lp[48]; path(lp, idx, file); buf = slurp(lp, &size); } while (0)
#endif

bool langLoad(int idx)
{
    if (idx == langCur()) return true;
    // the previous language goes first: two dictionaries at once would not fit in main RAM
    langApply(LANG_EN, NULL, NULL, NULL, 0);
    freeBuffers();
    lastMs = 0;
    if (idx == LANG_EN) return true;
    if (!langAvailable(idx)) return false;
    gfxLoading(langName(idx));
#ifdef ARM9
    cpuStartTiming(0);
#else
    clock_t t0 = clock();
#endif
    size_t nd = 0, np = 0, nl = 0;
    LOAD(idx, "dict.bin", bufDict, nd);
    LOAD(idx, "pools.bin", bufPools, np);
    LOAD(idx, "letters.bin", bufLetters, nl);
    (void)np; (void)nd;
    bool ok = bufDict && bufPools && bufLetters && langApply(idx, bufDict, bufPools, bufLetters, nl);
#ifdef ARM9
    lastMs = (int)(cpuEndTiming() / (BUS_CLOCK / 1000));
#else
    lastMs = (int)((clock() - t0) * 1000 / CLOCKS_PER_SEC);
#endif
    if (!ok) { freeBuffers(); langApply(LANG_EN, NULL, NULL, NULL, 0); return false; }
    return true;
}
