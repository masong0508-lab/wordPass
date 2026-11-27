// Independent numeric Classic records for integrated Russian/Ukrainian.
// Two alternating checked files per language; never modify wordPass.cfg.
#ifndef WORDPASS_SCRIPT_CAREER_H
#define WORDPASS_SCRIPT_CAREER_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define SCRIPT_CAREER_BYTES 44
typedef struct {
    uint32_t serial, attempts, cleared, words, peels, revealed, bestScore, bestShelf;
} ScriptCareer;
extern ScriptCareer scriptCareer[2];
extern bool scriptCareerSaveError[2];
bool scriptCareerEncode(unsigned language, const ScriptCareer *record, uint8_t out[SCRIPT_CAREER_BYTES]);
bool scriptCareerDecode(unsigned language,const uint8_t *data,size_t size,ScriptCareer *out);
void scriptCareerLoad(const char *settingsPath);
bool scriptCareerStore(const char *settingsPath,unsigned language);
void scriptCareerFinish(const char *settingsPath,unsigned language,unsigned score,unsigned shelf,
                        unsigned words,unsigned peels,unsigned revealed,bool passed);
#endif
