// UTF-8/Cyrillic font regression, runs natively in 'make check'.
#include "game.h"
#include <stdio.h>
static int fails;
#define CHECK(c,msg) do { if (!(c)) { printf("FAIL %s\n",msg); fails++; } } while (0)
static void checkText(const char *str, int want, int unknown)
{
    const char *p = str;
    int n = 0, missing = 0;
    while (*p) {
        const char *old = p;
        const u8 *g = fontGlyphNext(&p);
        CHECK(g && p > old && p-old <= 4, "UTF-8 decoder advances safely");
        missing += g == font5x7[39];
        n++;
    }
    CHECK(n == want && fontTextColumns(str) == want, "UTF-8 rendered character count");
    CHECK(unknown ? missing > 0 : missing == 0, "Cyrillic glyph coverage or fallback");
}
int main(void)
{
    checkText("HELLO 123", 9, 0);
    checkText("ПРИВЕТ", 6, 0);
    checkText("РУССКИЙ ЯЗЫК", 12, 0);
    checkText("ЁЖ", 2, 0);
    checkText("ПРИВІТ", 6, 0);
    checkText("УКРАЇНСЬКА", 10, 0);
    checkText("Ґ Є І Ї", 7, 0);
    checkText("ПʼЯТЬ М’ЯСО", 11, 0);
    CHECK(fontGlyphCodepoint(0x02bc) == font5x7[46] &&
          fontGlyphCodepoint(0x2019) == font5x7[46],
          "canonical and typographic apostrophes have the correct bitmap");
    // The opt-in Russian/Ukrainian goal HUD and help must fit the 206px
    // top-LCD plaque and use only supported 5x7 glyphs. UTF-8 bytes are
    // deliberately NOT equated with visible characters in these checks.
    checkText("НАЙДЕНО 0/9", 11, 0);
    checkText("ЗНАЙДЕНО 9/9", 12, 0);
    checkText("ЦЕЛЬ: ОТКРЫТЬ ВЕСЬ ПАРОЛЬ", 25, 0);
    checkText("МЕТА: ВІДКРИТИ ВЕСЬ ПАРОЛЬ", 26, 0);
    checkText("ТЯНИТЕ БУКВЫ / A: ПРИНЯТЬ", 25, 0);
    checkText("ТЯГНІТЬ ЛІТЕРИ / A: ПРИЙНЯТИ", 28, 0);
    checkText("СЛОВО ДАЁТ ЧИСЛО: +/- ПОДСКАЗКА", 31, 0);
    checkText("СЛОВО ДАЄ ЧИСЛО: +/- ПІДКАЗКА", 29, 0);
    CHECK(fontTextColumns("СЛОВО ДАЁТ ЧИСЛО: +/- ПОДСКАЗКА") * 6 <= 206,
          "Russian help line fits inside the top screen plaque");
    CHECK(fontTextColumns("СЛОВО ДАЄ ЧИСЛО: +/- ПІДКАЗКА") * 6 <= 206,
          "Ukrainian help line fits inside the top screen plaque");
    checkText("L+START: ЗБЕРЕГТИ / L+B: ВІДНОВИТИ", 34, 0);
    checkText("L+START: СОХР / L+B: ЗАГРУЗИТЬ", 30, 0);
    CHECK(fontTextColumns("L+START: ЗБЕРЕГТИ / L+B: ВІДНОВИТИ") * 6 <= 206,
          "Ukrainian checkpoint controls fit the help screen");
    checkText("ПОЧАТОК ПОЛИЦІ ЗБЕРЕЖЕНО", 24, 0);
    checkText("НАЧАЛО ПОЛКИ СОХРАНЕНО", 22, 0);
    checkText("привет", 6, 0);
    checkText("日本語", 3, 1);    // CJK needs a full-size atlas, not this 5x7 font.
    checkText("한글", 2, 1);      // Hangul needs composition and a larger atlas.
    checkText("\xC0\xAF", 2, 1); // overlong/stray continuation bytes
    checkText("\xD0", 1, 1);     // truncated
    checkText("\xE2\x82", 2, 1);
    checkText("\xED\xA0\x80", 1, 1); // surrogate
    checkText("\xF4\x90\x80\x80", 1, 1); // above U+10FFFF
    const char *s = "\xD0" "X";
    CHECK(fontGlyphNext(&s) == font5x7[39] && *s == 'X', "bad lead preserves next ASCII");
    CHECK(fontGlyphNext(&s) == font5x7[23] && *s == 0, "next ASCII stays readable");
    CHECK(fontTextColumns(NULL) == 0 && fontTextColumns("") == 0, "empty text");
    printf(fails ? "FONT UTF8 FAILED: %d\n" : "FONT UTF8 OK\n", fails);
    return fails != 0;
}
