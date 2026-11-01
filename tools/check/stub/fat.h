// Stand-in for libfat <fat.h> for tools/check/settings_test.c: the test decides whether a card is present.
#ifndef CHECK_STUB_FAT_H
#define CHECK_STUB_FAT_H
#include <stdbool.h>
extern bool stubFatOk;
static inline bool fatInitDefault(void) { return stubFatOk; }
#endif
