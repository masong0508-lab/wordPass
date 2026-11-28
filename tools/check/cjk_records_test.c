#include "script_career.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char dir[]="/tmp/wp-cjk-records-XXXXXX",cfg[128],path[128];
    assert(mkdtemp(dir));
    snprintf(cfg,sizeof cfg,"%s/wordPass.cfg",dir);
    scriptCareerLoad(cfg);
    const char *names[]={"RU","UK","ZS","ZT","JA","KO","HI","EL","TR"};
    for(unsigned lang=0;lang<SCRIPT_CAREER_COUNT;lang++) {
        ScriptCareer r={1,1,1,3,4,2,100+lang,1},out;
        uint8_t b[SCRIPT_CAREER_BYTES];
        assert(scriptCareerEncode(lang,&r,b));
        assert(scriptCareerDecode(lang,b,sizeof b,&out));
        assert(out.bestScore==100+lang);
        assert(!scriptCareerDecode((lang+1)%SCRIPT_CAREER_COUNT,b,sizeof b,&out));
        scriptCareerFinish(cfg,lang,100+lang,1,3,4,2,true);
        assert(scriptDailyCareerClaim(cfg,lang,20261127));
        assert(scriptDailyCareerFinish(cfg,lang,20261127,200+lang));
    }
    scriptCareerLoad(cfg);
    for(unsigned lang=0;lang<SCRIPT_CAREER_COUNT;lang++){
        assert(scriptCareer[lang].attempts==1&&scriptCareer[lang].bestScore==100+lang);
        assert(scriptDailyCareer[lang].results==1&&scriptDailyCareer[lang].bestScore==200+lang);
        assert(!scriptDailyCareerClaim(cfg,lang,20261127));
        assert(!scriptDailyCareerFinish(cfg,lang,20261127,999));
    }
    scriptCareerFinish(cfg,4,700,2,4,3,2,true);
    assert(scriptCareer[4].serial==2);
    snprintf(path,sizeof path,"%s/WPJA.C1",dir);
    FILE *f=fopen(path,"wb");assert(f&&fputc('W',f)!=EOF&&!fclose(f));
    scriptCareerLoad(cfg);
    assert(scriptCareer[4].serial==1&&scriptCareer[4].bestScore==104);
    assert(scriptCareer[3].bestScore==103);
    assert(scriptDailyCareerClaim(cfg,2,20261128));
    scriptCareerLoad(cfg);
    assert(!scriptDailyCareerClaim(cfg,2,20261128));
    assert(scriptDailyCareer[2].lastDate==20261127);
    assert(scriptDailyCareerClaim(cfg,3,20261128));
    assert(scriptDailyCareerFinish(cfg,3,20261128,310));
    assert(scriptDailyCareer[3].streak==2&&scriptDailyCareer[3].bestScore==310);
    assert(!scriptDailyCareerClaim(NULL,4,20261129));
    scriptCareerFinish(NULL,4,999,2,1,1,1,true);
    assert(scriptCareer[4].bestScore==999);
    assert(!scriptCareerStore(cfg,SCRIPT_CAREER_COUNT));
    assert(!scriptDailyCareerClaim(cfg,SCRIPT_CAREER_COUNT,20261129));
    assert(access(cfg,F_OK)!=0);
    for(unsigned lang=0;lang<SCRIPT_CAREER_COUNT;lang++)for(unsigned slot=0;slot<2;slot++){
        snprintf(path,sizeof path,"%s/WP%s.C%u",dir,names[lang],slot);unlink(path);
        snprintf(path,sizeof path,"%s/WP%s.D%u",dir,names[lang],slot);unlink(path);
    }
    assert(!rmdir(dir));
    puts("CJK CAREER OK: nine isolated locales, persistent records, date claims and torn-slot recovery");
}
