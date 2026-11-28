#include "script_career.h"
#include "script_daily.h"
#include <stdio.h>
#include <string.h>

ScriptCareer scriptCareer[SCRIPT_CAREER_COUNT];
bool scriptCareerSaveError[SCRIPT_CAREER_COUNT];
ScriptDailyCareer scriptDailyCareer[SCRIPT_CAREER_COUNT];
bool scriptDailySaveError[SCRIPT_CAREER_COUNT];
#define CAREER_MAX 999999999u
#define CAREER_PATH 128
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t v)
{
    for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i));
}
static uint32_t checksum(const uint8_t *p,size_t n)
{
    uint32_t h=2166136261u;
    while(n--) h=(h^*p++)*16777619u;
    return h;
}
static bool valid(const ScriptCareer *r)
{
    return r && r->attempts<=CAREER_MAX && r->cleared<=r->attempts &&
           r->words<=CAREER_MAX && r->peels<=CAREER_MAX &&
           r->revealed<=r->peels && r->bestScore<=CAREER_MAX &&
           r->bestShelf<=999 && (!r->bestShelf || r->cleared);
}
bool scriptCareerEncode(unsigned lang,const ScriptCareer *r,uint8_t out[SCRIPT_CAREER_BYTES])
{
    if(lang>=SCRIPT_CAREER_COUNT || !out || !valid(r)) return false;
    memcpy(out,"WCR1",4);
    out[4]=(uint8_t)lang; out[5]=out[6]=out[7]=0;
    const uint32_t v[8]={r->serial,r->attempts,r->cleared,r->words,r->peels,r->revealed,r->bestScore,r->bestShelf};
    for(unsigned i=0;i<8;++i) put32(out+8+4*i,v[i]);
    put32(out+40,checksum(out,40));
    return true;
}
bool scriptCareerDecode(unsigned lang,const uint8_t *b,size_t n,ScriptCareer *out)
{
    if(lang>=SCRIPT_CAREER_COUNT || !b || !out || n!=SCRIPT_CAREER_BYTES ||
       memcmp(b,"WCR1",4) || b[4]!=lang || b[5] || b[6] || b[7] ||
       get32(b+40)!=checksum(b,40)) return false;
    ScriptCareer r={get32(b+8),get32(b+12),get32(b+16),get32(b+20),
                    get32(b+24),get32(b+28),get32(b+32),get32(b+36)};
    if(!valid(&r)) return false;
    *out=r; return true;
}
static bool pathFor(const char *cfg,unsigned lang,unsigned slot,char kind,char path[CAREER_PATH])
{
    if(!cfg || lang>=SCRIPT_CAREER_COUNT || slot>1) return false;
    const char *slash=strrchr(cfg,'/');
    if(!slash) return false;
    size_t n=(size_t)(slash-cfg)+1;
    char leaf[]="WPRU.C0";
    static const char suffix[SCRIPT_CAREER_COUNT][2]={{'R','U'},{'U','K'},
#ifdef WP_CJK_MAIN
        {'Z','S'},{'Z','T'},{'J','A'},{'K','O'},{'H','I'},{'E','L'},{'T','R'},
#endif
    };
    leaf[2]=suffix[lang][0];leaf[3]=suffix[lang][1];
    leaf[5]=kind;
    leaf[6]=(char)('0'+slot);
    if(n>CAREER_PATH-sizeof leaf) return false;
    memcpy(path,cfg,n); memcpy(path+n,leaf,sizeof leaf);
    return true;
}
static bool readSlot(const char *cfg,unsigned lang,unsigned slot,ScriptCareer *r)
{
    char path[CAREER_PATH]; uint8_t data[SCRIPT_CAREER_BYTES+1];
    if(!pathFor(cfg,lang,slot,'C',path)) return false;
    FILE *f=fopen(path,"rb"); if(!f) return false;
    size_t n=fread(data,1,sizeof data,f);
    bool ok=!ferror(f); if(fclose(f)) ok=false;
    return ok && scriptCareerDecode(lang,data,n,r);
}
static int newest(const char *cfg,unsigned lang,ScriptCareer *r)
{
    ScriptCareer a,b;
    bool ha=readSlot(cfg,lang,0,&a),hb=readSlot(cfg,lang,1,&b);
    if(!ha && !hb) return -1;
    if(hb && (!ha || (b.serial-a.serial && b.serial-a.serial<0x80000000u))) {
        *r=b; return 1;
    }
    *r=a; return 0;
}
static int dailyNewest(const char *cfg,unsigned lang,ScriptDailyCareer *r);
void scriptCareerLoad(const char *cfg)
{
    memset(scriptCareer,0,sizeof scriptCareer);
    memset(scriptCareerSaveError,0,sizeof scriptCareerSaveError);
    memset(scriptDailyCareer,0,sizeof scriptDailyCareer);
    memset(scriptDailySaveError,0,sizeof scriptDailySaveError);
    if(cfg) for(unsigned i=0;i<SCRIPT_CAREER_COUNT;++i) {
        (void)newest(cfg,i,&scriptCareer[i]);
        (void)dailyNewest(cfg,i,&scriptDailyCareer[i]);
    }
}
bool scriptCareerStore(const char *cfg,unsigned lang)
{
    if(lang>=SCRIPT_CAREER_COUNT || !cfg) return false;
    ScriptCareer old, next=scriptCareer[lang];
    int slot=newest(cfg,lang,&old);
    next.serial=slot<0?1:old.serial+1;
    unsigned target=slot==0?1:0;
    char path[CAREER_PATH];uint8_t data[SCRIPT_CAREER_BYTES];
    bool ok=pathFor(cfg,lang,target,'C',path) && scriptCareerEncode(lang,&next,data);
    if(ok) {
        FILE *f=fopen(path,"wb");
        if(!f) ok=false;
        else {
            ok=fwrite(data,1,sizeof data,f)==sizeof data;
            if(fclose(f)) ok=false;
        }
    }
    ScriptCareer verify;
    if(ok) ok=readSlot(cfg,lang,target,&verify) &&
        verify.serial==next.serial && verify.attempts==next.attempts &&
        verify.cleared==next.cleared && verify.words==next.words &&
        verify.peels==next.peels && verify.revealed==next.revealed &&
        verify.bestScore==next.bestScore && verify.bestShelf==next.bestShelf;
    scriptCareerSaveError[lang]=!ok;
    if(ok) scriptCareer[lang].serial=next.serial;
    return ok;
}
static uint32_t plus(uint32_t a,unsigned b)
{
    return b>CAREER_MAX-a?CAREER_MAX:a+b;
}
void scriptCareerFinish(const char *cfg,unsigned lang,unsigned score,unsigned shelf,
                        unsigned words,unsigned peels,unsigned revealed,bool passed)
{
    if(lang>=SCRIPT_CAREER_COUNT || !shelf || shelf>999 || revealed>peels) return;
    ScriptCareer *r=&scriptCareer[lang];
    r->attempts=plus(r->attempts,1);
    if(passed) {
        r->cleared=plus(r->cleared,1);
        if(shelf>r->bestShelf) r->bestShelf=shelf;
    }
    r->words=plus(r->words,words);
    r->peels=plus(r->peels,peels);
    r->revealed=plus(r->revealed,revealed);
    if(score>r->bestScore) r->bestScore=score>CAREER_MAX?CAREER_MAX:score;
    if(cfg) (void)scriptCareerStore(cfg,lang);
}

// WDR1 retains the original Classic WCR1 format unchanged. In particular,
// the durable playedDate reservation is independent of the last completed
// score; a reset after claiming a date does not reopen that date.
static bool dailyValid(const ScriptDailyCareer *r)
{
    return r && scriptDailyValidDate((int)r->playedDate) &&
        (!r->lastDate || scriptDailyValidDate((int)r->lastDate)) &&
        (!r->bestDate || scriptDailyValidDate((int)r->bestDate)) &&
        r->bestDate<=r->lastDate && r->lastDate<=r->playedDate &&
        r->lastScore<=r->bestScore && r->bestScore<=CAREER_MAX &&
        r->streak<=9999 && r->streak<=r->results && r->results<=CAREER_MAX &&
        (r->lastDate!=0)==(r->results!=0) &&
        (r->bestDate!=0)==(r->results!=0);
}
static bool dailyRead(const char *cfg,unsigned lang,unsigned slot,ScriptDailyCareer *r)
{
    char path[CAREER_PATH];uint8_t data[SCRIPT_CAREER_BYTES+1];
    if(!pathFor(cfg,lang,slot,'D',path)) return false;
    FILE *f=fopen(path,"rb");
    if(!f) return false;
    size_t n=fread(data,1,sizeof data,f);
    bool ok=!ferror(f);if(fclose(f)) ok=false;
    if(!ok || n!=SCRIPT_CAREER_BYTES || memcmp(data,"WDR1",4) ||
       data[4]!=lang || data[5] || data[6] || data[7] ||
       get32(data+40)!=checksum(data,40)) return false;
    ScriptDailyCareer d={get32(data+8),get32(data+12),get32(data+16),
        get32(data+20),get32(data+24),get32(data+28),get32(data+32),get32(data+36)};
    if(!dailyValid(&d)) return false;
    *r=d;return true;
}
static int dailyNewest(const char *cfg,unsigned lang,ScriptDailyCareer *r)
{
    ScriptDailyCareer a,b;
    bool ha=dailyRead(cfg,lang,0,&a),hb=dailyRead(cfg,lang,1,&b);
    if(!ha && !hb) return -1;
    if(hb && (!ha || (b.serial-a.serial && b.serial-a.serial<0x80000000u))) {
        *r=b;return 1;
    }
    *r=a;return 0;
}
static bool dailyStore(const char *cfg,unsigned lang)
{
    if(!cfg || lang>=SCRIPT_CAREER_COUNT) return false;
    ScriptDailyCareer old,next=scriptDailyCareer[lang],verify;
    int slot=dailyNewest(cfg,lang,&old);
    next.serial=slot<0?1:old.serial+1;
    unsigned target=slot==0?1:0;
    char path[CAREER_PATH];uint8_t data[SCRIPT_CAREER_BYTES];
    bool ok=pathFor(cfg,lang,target,'D',path) && dailyValid(&next);
    if(ok) {
        memcpy(data,"WDR1",4);
        data[4]=(uint8_t)lang;data[5]=data[6]=data[7]=0;
        const uint32_t v[8]={next.serial,next.playedDate,next.lastDate,
            next.lastScore,next.bestScore,next.bestDate,next.streak,next.results};
        for(unsigned i=0;i<8;++i) put32(data+8+4*i,v[i]);
        put32(data+40,checksum(data,40));
        FILE *f=fopen(path,"wb");
        if(!f) ok=false;
        else {
            ok=fwrite(data,1,sizeof data,f)==sizeof data;
            if(fclose(f)) ok=false;
        }
    }
    if(ok) ok=dailyRead(cfg,lang,target,&verify) && !memcmp(&verify,&next,sizeof next);
    scriptDailySaveError[lang]=!ok;
    if(ok) scriptDailyCareer[lang].serial=next.serial;
    return ok;
}
bool scriptDailyCareerClaim(const char *cfg,unsigned lang,int date)
{
    if(!cfg || lang>=SCRIPT_CAREER_COUNT || !scriptDailyValidDate(date)) return false;
    ScriptDailyCareer *r=&scriptDailyCareer[lang];
    if((unsigned)date<=r->playedDate) return false;
    r->playedDate=(unsigned)date;
    // Even if the verification fails, refuse another scored attempt in
    // this session: the write may already have reached the card.
    return dailyStore(cfg,lang);
}
static bool consecutive(unsigned a,unsigned b)
{
    if(!a || b<=a) return false;
    if(b==a+1) return true;
    if(scriptDailyValidDate((int)(a+1))) return false;
    unsigned month=a/100%100;
    unsigned next=month==12?(a/10000+1)*10000+101:(a/100+1)*100+1;
    return b==next;
}
bool scriptDailyCareerFinish(const char *cfg,unsigned lang,int date,unsigned score)
{
    if(!cfg || lang>=SCRIPT_CAREER_COUNT || !scriptDailyValidDate(date)) return false;
    ScriptDailyCareer *r=&scriptDailyCareer[lang];
    if(r->playedDate!=(unsigned)date || r->lastDate>=(unsigned)date) return false;
    if(score>CAREER_MAX) score=CAREER_MAX;
    r->streak=consecutive(r->lastDate,(unsigned)date)?
        (r->streak<9999?r->streak+1:9999):1;
    r->lastDate=(unsigned)date;
    r->lastScore=score;
    if(!r->bestDate || score>r->bestScore) {r->bestScore=score;r->bestDate=(unsigned)date;}
    r->results=plus(r->results,1);
    return dailyStore(cfg,lang);
}
