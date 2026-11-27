// Native save-format and interrupted-write recovery regression.
#include "script_career.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void)
{
    char dir[]="/tmp/wp-career-XXXXXX";
    assert(mkdtemp(dir));
    char cfg[128], path[128];
    snprintf(cfg,sizeof cfg,"%s/wordPass.cfg",dir);
    scriptCareerLoad(cfg);
    assert(!scriptCareer[0].attempts && !scriptCareer[1].attempts);
    scriptCareerFinish(cfg,0,321,1,2,4,4,true);
    assert(scriptCareer[0].serial==1 && scriptCareer[0].cleared==1);
    assert(!scriptCareer[1].attempts);
    scriptCareerFinish(cfg,0,456,2,3,2,1,false);
    scriptCareerFinish(cfg,1,123,1,1,4,4,true);
    assert(scriptCareer[0].serial==2 && scriptCareer[0].attempts==2);
    assert(scriptCareer[0].bestScore==456 && scriptCareer[0].bestShelf==1);
    assert(scriptCareer[0].words==5 && scriptCareer[0].peels==6 && scriptCareer[0].revealed==5);
    uint8_t b[SCRIPT_CAREER_BYTES];
    ScriptCareer r;
    assert(scriptCareerEncode(0,&scriptCareer[0],b));
    assert(scriptCareerDecode(0,b,sizeof b,&r) && r.bestScore==456);
    assert(!scriptCareerDecode(1,b,sizeof b,&r));
    assert(!scriptCareerDecode(0,b,sizeof b-1,&r));
    b[18]^=1;
    assert(!scriptCareerDecode(0,b,sizeof b,&r));
    scriptCareerLoad(cfg);
    assert(scriptCareer[0].bestScore==456 && scriptCareer[1].bestScore==123);
    // Tear the most recent Russian slot; the older complete slot survives.
    snprintf(path,sizeof path,"%s/WPRU.C1",dir);
    FILE *f=fopen(path,"wb"); assert(f);
    assert(fputc('W',f)!=EOF && !fclose(f));
    scriptCareerLoad(cfg);
    assert(scriptCareer[0].serial==1 && scriptCareer[0].attempts==1);
    assert(scriptCareer[0].bestScore==321 && scriptCareer[1].bestScore==123);
    scriptCareerFinish(cfg,0,555,2,2,4,4,true);
    scriptCareerLoad(cfg);
    assert(scriptCareer[0].serial==2 && scriptCareer[0].attempts==2);
    assert(scriptCareer[0].bestScore==555 && scriptCareer[0].bestShelf==2);
    // Scored Daily reserves a date durably before any result exists.
    assert(!scriptDailyCareerClaim(NULL,0,20280228));
    assert(!scriptDailyCareerClaim(cfg,0,20280230));
    assert(scriptDailyCareerClaim(cfg,0,20280228));
    scriptCareerLoad(cfg);
    assert(scriptDailyCareer[0].playedDate==20280228 && !scriptDailyCareer[0].lastDate);
    assert(!scriptDailyCareerClaim(cfg,0,20280228));
    assert(!scriptDailyCareerClaim(cfg,0,20280227));
    assert(scriptDailyCareerFinish(cfg,0,20280228,0));
    assert(!scriptDailyCareerFinish(cfg,0,20280228,900));
    assert(scriptDailyCareerClaim(cfg,0,20280229));
    assert(scriptDailyCareerFinish(cfg,0,20280229,400));
    assert(scriptDailyCareerClaim(cfg,0,20280301));
    assert(scriptDailyCareerFinish(cfg,0,20280301,250));
    assert(scriptDailyCareer[0].streak==3 && scriptDailyCareer[0].bestScore==400);
    assert(scriptDailyCareer[0].bestDate==20280229 && scriptDailyCareer[0].results==3);
    assert(scriptDailyCareerClaim(cfg,1,20280301));
    assert(scriptDailyCareerFinish(cfg,1,20280301,100));
    assert(scriptDailyCareer[1].streak==1 && scriptDailyCareer[1].bestScore==100);
    assert(scriptDailyCareerClaim(cfg,0,20280302));
    scriptCareerLoad(cfg); // simulates reset with a consumed but unfinished day
    assert(!scriptDailyCareerClaim(cfg,0,20280302));
    assert(scriptDailyCareer[0].lastDate==20280301);
    assert(scriptDailyCareerClaim(cfg,0,20280303));
    assert(scriptDailyCareerFinish(cfg,0,20280303,500));
    assert(scriptDailyCareer[0].streak==1 && scriptDailyCareer[0].bestScore==500);
    // D0 holds serial 9 (result); D1 holds serial 8 (date reservation).
    snprintf(path,sizeof path,"%s/WPRU.D0",dir);
    f=fopen(path,"wb");assert(f);
    assert(fputc('W',f)!=EOF && !fclose(f));
    scriptCareerLoad(cfg);
    assert(scriptDailyCareer[0].playedDate==20280303);
    assert(!scriptDailyCareerClaim(cfg,0,20280303));
    assert(scriptDailyCareer[1].bestScore==100);
    assert(scriptCareer[0].bestScore==555 && scriptCareer[1].bestScore==123);
    assert(access(cfg,F_OK)!=0); // never touch the Latin settings file
    for(unsigned lang=0;lang<2;++lang) for(unsigned slot=0;slot<2;++slot) {
        snprintf(path,sizeof path,"%s/WP%s.C%u",dir,lang?"UK":"RU",slot);
        unlink(path);
        snprintf(path,sizeof path,"%s/WP%s.D%u",dir,lang?"UK":"RU",slot);
        unlink(path);
    }
    assert(!rmdir(dir));
    scriptCareerLoad(NULL);
    scriptCareerFinish(NULL,1,999,1,1,4,4,true);
    assert(scriptCareer[1].bestScore==999 && !scriptCareer[1].serial);
    puts("CAREER OK: language isolation, checked format, FAT slot fallback, in-memory no-card mode");
    return 0;
}
