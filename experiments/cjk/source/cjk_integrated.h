#ifndef WORDPASS_CJK_INTEGRATED_H
#define WORDPASS_CJK_INTEGRATED_H
#include <nds.h>
#include <stdbool.h>
#define CJ_LOCALES 7
const char *cjkLocaleName(int locale);
void cjkProbe(void);
bool cjkAvailable(int locale);
bool cjkEnter(int locale,u32 seed,int dailyDate,bool training,int difficulty,const char *cfg);
extern int cjkDailyRanked; // 0 practice, 1 ranked attempt, 2 result recorded
void cjkLeave(void);
bool cjkInput(u32 down,u32 held,u32 up,const touchPosition *t);
void cjkDraw(u16 *target,bool upper,int y0,int y1);
extern unsigned cjkEpoch;
#endif
