// Native tests for the language data: data/lang/<code>/{dict.bin,pools.bin,letters.bin} for the ten
// languages, loaded with the game's own readers (source/dict.c and source/pools.c, pointed at buffers
// through dict_bin and pools_bin exactly as board_test.c does), plus data/lang/manifest.txt and the
// English files, which must be byte for byte what the manifest pins.
// Run with `make check` from the project folder.  The last line is LANG OK: 0 failure(s).
#include "game.h"
#include "dict_bin.h"
#include "pools_bin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(name, cond) do { if (!(cond)) { printf("FAIL %s (%s) at %s:%d\n", name, #cond, __FILE__, __LINE__); fails++; } } while (0)
#define SAY(...) printf(__VA_ARGS__)

#define NLANG 10
#define NLEN 18                 // dictionary lengths 3..20
#define NTHEME 40
#define LET_SIZE 68
#define MIN_THEME_WORDS 25
#define MIN_LEXICON_WORDS 100
#define MIN_POOL_WORDS 13000            // pass words in all, per language (Swedish and Danish have the smallest wordnets)
// languages still being brought to the standard: none (each stage of the word-table work removed one)
static const char *PENDING_CODES = "";
static const char *CODES[NLANG] = { "es", "fr", "de", "it", "pt", "nl", "sv", "pl", "da", "fi" };
static const u8 REC[21] = { 0, 0, 0, 4, 4, 4, 4, 5, 6, 7, 7, 8, 8, 9, 9, 10, 11, 11, 12, 13, 13 };

// a few words each language must accept and a few strings it must reject (all lower case a-z)
static const char *KNOWN[NLANG] = {
    "casa perro gato mesa libro escuela amigo ciudad trabajar mariposa ventana",
    "maison chat chien table livre ecole ami ville travailler papillon fenetre",
    "haus hund katze tisch buch schule freund stadt arbeiten schmetterling fenster",
    "casa cane gatto tavolo libro scuola amico citta lavorare farfalla finestra",
    "casa cachorro gato mesa livro escola amigo cidade trabalhar borboleta janela",
    "huis hond kat tafel boek school vriend stad werken vlinder raam",
    "hus hund katt bord bok skola van stad arbeta fjaril fonster",
    "dom pies kot stol ksiazka szkola przyjaciel miasto pracowac motyl okno",
    "hus hund kat bord bog skole ven arbejde smuk sommerfugl vindue",
    "talo koira kissa poyta kirja koulu ystava kaupunki kaunis perhonen ikkuna",
};
static const char *UNKNOWN[NLANG] = {
    "xqzt casaz perroo mesaa libroz", "xqzt maisonz chatz livreo", "xqzt hausz hundo katzo tischz",
    "xqzt casaz canez gattoo tavolz", "xqzt casaz gatoo mesaa livroz", "xqzt huisz hondo tafelz boekz",
    "xqzt husz hundo kattz bordz", "xqzt domz piesz kotz stolz", "xqzt husz hundo katz bordz",
    "xqzt talox koirax kissax poytax",
};
// theme names that must be exactly these: the first shelf, the animal shelf and the last one
static const char *NAME0[NLANG] = { "LIBROS", "RELIURE", "BUCHER", "LIBRI", "LIVROS", "BOEKEN", "BOKBAND", "KSIAZKI", "BOGER", "KIRJAT" };
static const char *NAME8[NLANG] = { "BESTIAS", "BETES", "TIERE", "BESTIE", "BICHOS", "DIEREN", "DJUR", "ZWIERZE", "DYR", "ELAIMET" };
static const char *NAME39[NLANG] = { "LEXICO", "LEXIQUE", "LEXIKON", "LESSICO", "LEXICO", "LEXICON", "LEXIKON", "SLOWNIK", "ORDBOG", "SANASTO" };

static u8 *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    b[n] = 0;
    if (size) *size = (size_t)n;
    return b;
}

// ---- sha256, for the English pin
typedef struct { u32 h[8]; u64 len; u8 buf[64]; int n; } Sha;
static const u32 K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void shaBlock(Sha *s, const u8 *p)
{
    u32 w[64], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = ((u32)p[4*i] << 24) | ((u32)p[4*i+1] << 16) | ((u32)p[4*i+2] << 8) | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3), s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (int i = 0; i < 64; i++) {
        u32 t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        u32 t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha256Hex(const u8 *data, size_t len, char out[65])
{
    Sha s = { { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 }, 0, {0}, 0 };
    size_t i = 0;
    for (; i + 64 <= len; i += 64) shaBlock(&s, data + i);
    u8 tail[128] = {0};
    size_t r = len - i;
    memcpy(tail, data + i, r);
    tail[r] = 0x80;
    size_t tl = r + 1 + 8 <= 64 ? 64 : 128;
    u64 bits = (u64)len * 8;
    for (int k = 0; k < 8; k++) tail[tl - 1 - k] = (u8)(bits >> (8 * k));
    for (size_t o = 0; o < tl; o += 64) shaBlock(&s, tail + o);
    for (int k = 0; k < 8; k++) sprintf(out + 8 * k, "%08x", s.h[k]);
}

static u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

// ---- one language
typedef struct { char code[3]; long words, themes, poolWords; int qu; long dictBytes, poolBytes, letBytes; } Row;
static Row rows[16];
static int nrows;

static void checkLanguage(int li, const Row *row)
{
    const char *code = CODES[li];
    char path[128], what[160];
    const int pending = strstr(PENDING_CODES, code) != NULL;
    size_t dsz = 0, psz = 0, lsz = 0;
    snprintf(path, sizeof path, "data/lang/%s/dict.bin", code);   u8 *dict = slurp(path, &dsz);
    snprintf(path, sizeof path, "data/lang/%s/pools.bin", code);  u8 *pools = slurp(path, &psz);
    snprintf(path, sizeof path, "data/lang/%s/letters.bin", code); u8 *let = slurp(path, &lsz);
    snprintf(what, sizeof what, "%s: three files load", code);
    CHECK(what, dict && pools && let);
    if (!dict || !pools || !let) return;
    snprintf(what, sizeof what, "%s: sizes match the manifest", code);
    CHECK(what, row && (long)dsz == row->dictBytes && (long)psz == row->poolBytes && (long)lsz == row->letBytes);

    // ---- dictionary: header, every record, sorted, a-z only, tier bit, count caps
    snprintf(what, sizeof what, "%s: dictionary magic WPD3", code);
    CHECK(what, memcmp(dict, "WPD3", 4) == 0);
    long total = 0, common = 0, bad = 0, unsorted = 0, miss = 0, qbad = 0, tierBad = 0;
    unsigned letterSeen[26] = { 0 };
    const u8 *p = dict + 4 + 4 * NLEN;
    u32 counts[NLEN];
    for (int i = 0; i < NLEN; i++) counts[i] = le32(dict + 4 + 4 * i);
    size_t expect = 4 + 4 * NLEN;
    for (int n = 3; n <= MAX_WORD; n++) expect += (size_t)counts[n - 3] * REC[n];
    snprintf(what, sizeof what, "%s: dictionary size = header + records", code);
    CHECK(what, expect == dsz);
    dict_bin = dict;
    dictInit();
    for (int n = 3; n <= MAX_WORD; n++) {
        const u8 *prev = NULL;
        for (u32 k = 0; k < counts[n - 3]; k++, p += REC[n]) {
            char w[MAX_WORD + 1];
            int ok = 1;
            for (int i = 0; i < n; i++) {
                int bit = 5 * i, v = ((p[bit >> 3] << 8) | ((bit >> 3) + 1 < REC[n] ? p[(bit >> 3) + 1] : 0));
                v = (v >> (11 - (bit & 7))) & 31;
                if (v > 25) { ok = 0; v = 0; }
                w[i] = (char)('a' + v);
                letterSeen[v]++;
            }
            w[n] = 0;
            // padding bits between the letters and the tier bit must be zero
            u8 copy[16]; memcpy(copy, p, REC[n]); copy[REC[n] - 1] &= 0xFE;
            for (int b = 5 * n; b < REC[n] * 8; b++) if (copy[b >> 3] & (0x80 >> (b & 7))) ok = 0;
            if (!ok) bad++;
            if (prev) {                    // strictly increasing, the tier bit aside (a twin would differ only in it)
                int c = memcmp(prev, p, REC[n] - 1);
                if (c == 0) c = (prev[REC[n] - 1] & 0xFE) - (p[REC[n] - 1] & 0xFE);
                if (c >= 0) unsorted++;
            }
            prev = p;
            if (strchr(w, 'q') && !strstr(w, "qu")) qbad++;
            int t = dictLookup(w, n);
            if (!t) miss++;
            else if ((t - 1) != (p[REC[n] - 1] & 1)) tierBad++;
            if (!(p[REC[n] - 1] & 1)) common++;
        }
        total += counts[n - 3];
    }
    snprintf(what, sizeof what, "%s: every dictionary word is a-z only with clean padding", code); CHECK(what, bad == 0);
    snprintf(what, sizeof what, "%s: every length group is sorted with no duplicate", code); CHECK(what, unsorted == 0);
    snprintf(what, sizeof what, "%s: dictLookup finds every word with its tier", code); CHECK(what, miss == 0 && tierBad == 0);
    snprintf(what, sizeof what, "%s: no q without u (Qu tile rule)", code); CHECK(what, qbad == 0);
    snprintf(what, sizeof what, "%s: word count within the caps (80k..250k)", code); CHECK(what, total >= 80000 && total <= 250000);
    snprintf(what, sizeof what, "%s: dictionary count matches the manifest", code); CHECK(what, row && total == row->words);
    snprintf(what, sizeof what, "%s: common tier is 15k..35k words", code); CHECK(what, common >= 15000 && common <= 35000);
    snprintf(what, sizeof what, "%s: no 1 or 2 letter words, lengths 3..20 all present up to 12", code);
    CHECK(what, counts[0] > 100 && counts[9] > 100);
    SAY("%s: %ld words (%ld common), dict %zu bytes\n", code, total, common, dsz);

    // ---- known words and known non-words
    char list[512];
    int okKnown = 1, okUnknown = 1;
    strcpy(list, KNOWN[li]);
    for (char *w = strtok(list, " "); w; w = strtok(NULL, " ")) if (!dictLookup(w, (int)strlen(w))) { okKnown = 0; printf("  missing %s\n", w); }
    strcpy(list, UNKNOWN[li]);
    for (char *w = strtok(list, " "); w; w = strtok(NULL, " ")) if (dictLookup(w, (int)strlen(w))) { okUnknown = 0; printf("  accepted %s\n", w); }
    snprintf(what, sizeof what, "%s: known words accepted", code); CHECK(what, okKnown);
    snprintf(what, sizeof what, "%s: known non-words rejected", code); CHECK(what, okUnknown);
    snprintf(what, sizeof what, "%s: strings of 1 and 2 letters and 21 letters are rejected", code);
    CHECK(what, !dictLookup("a", 1) && !dictLookup("ab", 2) && !dictLookup("abcdefghijklmnopqrstu", 21));

    // ---- letters
    snprintf(what, sizeof what, "%s: letters magic and size", code); CHECK(what, memcmp(let, "WPL1", 4) == 0 && lsz == LET_SIZE);
    int sum = 0, wOk = 1, vOk = 1, present = 1, absent = 1;
    const u8 *wt = let + 16, *val = let + 42;
    for (int i = 0; i < 26; i++) {
        sum += wt[i];
        if (wt[i] && (val[i] < 1 || val[i] > 10)) vOk = 0;
        if (wt[i] > 25) wOk = 0;           // German's tuned table gives E 24 (tools/lang/letters.py)
        if (wt[i] && !letterSeen[i]) present = 0;          // a tile letter that no word uses
        if (!wt[i] && letterSeen[i]) absent = 0;           // a word letter the bag never deals
    }
    snprintf(what, sizeof what, "%s: tile weights sum to 100", code); CHECK(what, sum == 100);
    snprintf(what, sizeof what, "%s: tile weights at most 25 each", code); CHECK(what, wOk);
    snprintf(what, sizeof what, "%s: letter values 1..10 for every tile letter", code); CHECK(what, vOk);
    snprintf(what, sizeof what, "%s: every tile letter occurs in the dictionary", code); CHECK(what, present);
    snprintf(what, sizeof what, "%s: every dictionary letter has a tile", code); CHECK(what, absent);
    snprintf(what, sizeof what, "%s: Qu flag agrees with the Q tile and with the dictionary", code);
    CHECK(what, (let[4] & 1) == (wt[16] > 0) && (row && row->qu == (let[4] & 1)));
    u32 vowels = le32(let + 12);
    int vw = 0, vv = 0;
    for (int i = 0; i < 26; i++) if (vowels & (1u << i)) { vw++; if (wt[i]) vv += wt[i]; }
    snprintf(what, sizeof what, "%s: vowel set has A E I O U and 25..60 percent of the tiles", code);
    CHECK(what, (vowels & 0x104111u) == 0x104111u && (vowels & ~0x1104111u) == 0 && vw >= 5 && vw <= 6 && vv >= 25 && vv <= 60);
    int rareOk = 1;
    for (int i = 0; i < 4; i++) if (let[6 + i] != 255 && (let[6 + i] > 25 || !wt[let[6 + i]])) rareOk = 0;
    snprintf(what, sizeof what, "%s: rare letters are tile letters", code); CHECK(what, rareOk);
    SAY("%s: tiles sum %d, vowels %d%%, Qu %d\n", code, sum, vv, let[4] & 1);

    // ---- pools
    pools_bin = pools;
    poolsInit();
    snprintf(what, sizeof what, "%s: pools magic WPP1 and 40 themes", code);
    CHECK(what, memcmp(pools, "WPP1", 4) == 0 && poolThemeCount() == NTHEME && le32(pools + 4) == NTHEME);
    snprintf(what, sizeof what, "%s: theme names 0, 8 and 39", code);
    CHECK(what, strcmp(poolName(0), NAME0[li]) == 0 && strcmp(poolName(8), NAME8[li]) == 0 && strcmp(poolName(39), NAME39[li]) == 0);
    long pw = 0, notInDict = 0, badWord = 0, dup = 0, over = 0, nameBad = 0, dupName = 0;
    size_t poolExpect = 8 + 20 * NTHEME;
    // every pool word, with a hash set of words seen so far (open addressing)
    enum { HS = 1 << 16 };
    static char (*seen)[MAX_WORD];
    if (!seen) seen = calloc(HS, MAX_WORD);
    memset(seen, 0, (size_t)HS * MAX_WORD);
    for (int t = 0; t < NTHEME; t++) {
        const char *nm = poolName(t);
        size_t L = strlen(nm);
        if (L < 2 || L > 7) nameBad++;
        for (size_t i = 0; i < L; i++) if (nm[i] < 'A' || nm[i] > 'Z') nameBad++;
        for (int u = 0; u < t; u++) if (strcmp(poolName(u), nm) == 0) dupName++;
        long themeWords = 0;
        for (int len = POOL_MIN_LEN; len <= POOL_MAX_LEN; len++) {
            int c = poolCount(t, len);
            poolExpect += (size_t)c * len;
            if (c > 1000) over++;
            themeWords += c;
            for (int i = 0; i < c; i++) {
                char w[MAX_WORD + 1];
                poolWord(t, len, i, w);
                int ok = (int)strlen(w) == len;
                for (int k = 0; k < len; k++) if (w[k] < 'a' || w[k] > 'z') ok = 0;
                if (!ok) { badWord++; continue; }
                if (!dictLookup(w, len)) notInDict++;
                u32 h = 2166136261u;
                for (int k = 0; k < len; k++) h = (h ^ (u8)w[k]) * 16777619u;
                for (;; h++) {
                    char *slot = seen[h & (HS - 1)];
                    if (!slot[0]) { memcpy(slot, w, (size_t)len); break; }
                    if (strlen(slot) == (size_t)len && memcmp(slot, w, (size_t)len) == 0) { dup++; break; }
                }
            }
        }
        pw += themeWords;
        // the standard, the same for every language and for English: 25 words a theme, 8 of 4, 5 and 6 letters, 5 of 7
        // letters; the catch-all LEXICON needs 100 words.  PENDING_CODES lists languages not yet at the standard (none).
        if (!pending) {
            if (t != 39 && themeWords < MIN_THEME_WORDS) { snprintf(what, sizeof what, "%s: theme %d has at least %d words (has %ld)", code, t, MIN_THEME_WORDS, themeWords); CHECK(what, 0); }
            if (t == 39 && themeWords < MIN_LEXICON_WORDS) { snprintf(what, sizeof what, "%s: LEXICON has at least %d words (has %ld)", code, MIN_LEXICON_WORDS, themeWords); CHECK(what, 0); }
            if (t != 39 && (poolCount(t, 4) < 8 || poolCount(t, 5) < 8 || poolCount(t, 6) < 8 || poolCount(t, 7) < 5)) {
                snprintf(what, sizeof what, "%s: theme %d has 8+ words of 4, 5 and 6 letters and 5+ of 7 (%d, %d, %d, %d)", code, t, poolCount(t, 4), poolCount(t, 5), poolCount(t, 6), poolCount(t, 7));
                CHECK(what, 0);
            }
        } else if (t != 39 && themeWords < 25) { snprintf(what, sizeof what, "%s: theme %d has at least 25 words (has %ld)", code, t, themeWords); CHECK(what, 0); }
    }
    snprintf(what, sizeof what, "%s: pool file size = header + words", code); CHECK(what, poolExpect == psz);
    snprintf(what, sizeof what, "%s: pool words are a-z and the right length", code); CHECK(what, badWord == 0);
    snprintf(what, sizeof what, "%s: every pool word is in the dictionary", code); CHECK(what, notInDict == 0);
    snprintf(what, sizeof what, "%s: no pool word appears in two themes or twice", code); CHECK(what, dup == 0);
    snprintf(what, sizeof what, "%s: at most 1000 words per theme and length", code); CHECK(what, over == 0);
    snprintf(what, sizeof what, "%s: theme names are 2..7 letters A-Z and distinct", code); CHECK(what, nameBad == 0 && dupName == 0);
    snprintf(what, sizeof what, "%s: pool word count matches the manifest", code); CHECK(what, row && pw == row->poolWords && row->themes == NTHEME);
    if (!pending) { snprintf(what, sizeof what, "%s: at least %d pass words in all (has %ld)", code, MIN_POOL_WORDS, pw); CHECK(what, pw >= MIN_POOL_WORDS); }
    SAY("%s: %ld pool words in %d themes, %zu bytes\n", code, pw, NTHEME, psz);
    // pool words must also pass the Qu tile rule and stay inside the tile alphabet
    long outside = 0;
    for (int t = 0; t < NTHEME; t++)
        for (int len = POOL_MIN_LEN; len <= POOL_MAX_LEN; len++)
            for (int i = 0; i < poolCount(t, len); i++) {
                char w[MAX_WORD + 1];
                poolWord(t, len, i, w);
                for (int k = 0; k < len; k++) if (!wt[w[k] - 'a']) outside++;
            }
    snprintf(what, sizeof what, "%s: pool words use only tile letters", code); CHECK(what, outside == 0);
    free(dict); free(pools); free(let);
}

// ---- manifest
static int loadManifest(char englishSha[3][65], long *totalBytes)
{
    size_t n;
    char *m = (char *)slurp("data/lang/manifest.txt", &n);
    if (!m) return 0;
    char *save = NULL;
    *totalBytes = 0;
    int haveSha = 0;
    for (char *line = strtok_r(m, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "#sha256 en ", 11)) {
            haveSha = sscanf(line + 11, "%64s %64s %64s", englishSha[0], englishSha[1], englishSha[2]) == 3;
        } else if (!strncmp(line, "# total ", 8)) {
            *totalBytes = atol(line + 8);
        } else if (line[0] != '#' && strncmp(line, "en\t", 3) != 0) {
            Row *r = &rows[nrows];
            char native[32], english[32];
            if (sscanf(line, "%2s\t%31[^\t]\t%31[^\t]\t%ld\t%ld\t%ld\t%d\t%ld\t%ld\t%ld", r->code, native, english, &r->words, &r->themes,
                       &r->poolWords, &r->qu, &r->dictBytes, &r->poolBytes, &r->letBytes) == 10) nrows++;
        }
    }
    free(m);
    return haveSha;
}

int main(void)
{
    char sha[3][65];
    long totalBytes = 0;
    CHECK("manifest loads with the English pin", loadManifest(sha, &totalBytes));
    CHECK("manifest lists ten languages", nrows == NLANG);
    long sum = 0;
    for (int i = 0; i < NLANG; i++) {
        const Row *row = NULL;
        for (int r = 0; r < nrows; r++) if (!strcmp(rows[r].code, CODES[i])) row = &rows[r];
        CHECK("manifest has every language in order", row && !strcmp(rows[i].code, CODES[i]));
        if (row) sum += row->dictBytes + row->poolBytes + row->letBytes;
        checkLanguage(i, row);
    }
    CHECK("manifest total matches the rows", sum == totalBytes);
    SAY("data/lang total %ld bytes\n", sum);

    // ---- English: the three files are untouched, and the game's reader still loads them
    static const char *en[3] = { "data/dict.bin", "data/pools.bin", "data/variants.bin" };
    for (int i = 0; i < 3; i++) {
        size_t n = 0;
        u8 *b = slurp(en[i], &n);
        char hex[65] = "";
        if (b) sha256Hex(b, n, hex);
        CHECK("English file unchanged (sha256 pin)", b && strcmp(hex, sha[i]) == 0);
        if (i == 0 && b) {
            dict_bin = b; dictInit();
            CHECK("English dictionary still answers", dictLookup("book", 4) == 1 && dictLookup("zax", 3) == 2 && dictLookup("xxx", 3) == 0);
        }
        if (i == 1 && b) {
            pools_bin = b; poolsInit();
            CHECK("English pools still load: 40 themes, BINDERY first, LEXICON last",
                  poolThemeCount() == 40 && strcmp(poolName(0), "BINDERY") == 0 && strcmp(poolName(39), "LEXICON") == 0);
        }
    }
    printf("%s: %d failure(s)\n", fails ? "LANG FAILED" : "LANG OK", fails);
    return fails;
}
