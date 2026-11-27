// Independent numeric Classic records for integrated Russian/Ukrainian.
// Two alternating checked files per language; never modify wordPass.cfg.
#ifndef WORDPASS_SCRIPT_CAREER_H
#define WORDPASS_SCRIPT_CAREER_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define SCRIPT_CAREER_BYTES 44
#ifdef WP_CJK_MAIN
#define SCRIPT_CAREER_COUNT 5
#else
#define SCRIPT_CAREER_COUNT 2
#endif
typedef struct {
    uint32_t serial, attempts, cleared, words, peels, revealed, bestScore, bestShelf;
} ScriptCareer;
extern ScriptCareer scriptCareer[SCRIPT_CAREER_COUNT];
extern bool scriptCareerSaveError[SCRIPT_CAREER_COUNT];
bool scriptCareerEncode(unsigned language, const ScriptCareer *record, uint8_t out[SCRIPT_CAREER_BYTES]);
bool scriptCareerDecode(unsigned language,const uint8_t *data,size_t size,ScriptCareer *out);
void scriptCareerLoad(const char *settingsPath);
bool scriptCareerStore(const char *settingsPath,unsigned language);
void scriptCareerFinish(const char *settingsPath,unsigned language,unsigned score,unsigned shelf,
                        unsigned words,unsigned peels,unsigned revealed,bool passed);
// A separate dated score file uses the same two-slot storage discipline.
typedef struct {
    uint32_t serial, playedDate, lastDate, lastScore, bestScore, bestDate, streak, results;
} ScriptDailyCareer;
extern ScriptDailyCareer scriptDailyCareer[SCRIPT_CAREER_COUNT];
extern bool scriptDailySaveError[SCRIPT_CAREER_COUNT];
// Claim only after the board was generated and the date write is verified.
// No card or repeated/backdated attempt => practice, never a scored attempt.
bool scriptDailyCareerClaim(const char *cfg,unsigned language,int date);
bool scriptDailyCareerFinish(const char *cfg,unsigned language,int date,unsigned score);
#endif
