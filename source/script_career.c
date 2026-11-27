#include "script_career.h"
#include <stdio.h>
#include <string.h>

ScriptCareer scriptCareer[2];
bool scriptCareerSaveError[2];
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
    if(lang>1 || !out || !valid(r)) return false;
    memcpy(out,"WCR1",4);
    out[4]=(uint8_t)lang; out[5]=out[6]=out[7]=0;
    const uint32_t v[8]={r->serial,r->attempts,r->cleared,r->words,r->peels,r->revealed,r->bestScore,r->bestShelf};
    for(unsigned i=0;i<8;++i) put32(out+8+4*i,v[i]);
    put32(out+40,checksum(out,40));
    return true;
}
bool scriptCareerDecode(unsigned lang,const uint8_t *b,size_t n,ScriptCareer *out)
{
    if(lang>1 || !b || !out || n!=SCRIPT_CAREER_BYTES ||
       memcmp(b,"WCR1",4) || b[4]!=lang || b[5] || b[6] || b[7] ||
       get32(b+40)!=checksum(b,40)) return false;
    ScriptCareer r={get32(b+8),get32(b+12),get32(b+16),get32(b+20),
                    get32(b+24),get32(b+28),get32(b+32),get32(b+36)};
    if(!valid(&r)) return false;
    *out=r; return true;
}
static bool pathFor(const char *cfg,unsigned lang,unsigned slot,char path[CAREER_PATH])
{
    if(!cfg || lang>1 || slot>1) return false;
    const char *slash=strrchr(cfg,'/');
    if(!slash) return false;
    size_t n=(size_t)(slash-cfg)+1;
    char leaf[]="WPRU.C0";
    if(lang) {leaf[2]='U';leaf[3]='K';}
    leaf[6]=(char)('0'+slot);
    if(n>CAREER_PATH-sizeof leaf) return false;
    memcpy(path,cfg,n); memcpy(path+n,leaf,sizeof leaf);
    return true;
}
static bool readSlot(const char *cfg,unsigned lang,unsigned slot,ScriptCareer *r)
{
    char path[CAREER_PATH]; uint8_t data[SCRIPT_CAREER_BYTES+1];
    if(!pathFor(cfg,lang,slot,path)) return false;
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
void scriptCareerLoad(const char *cfg)
{
    memset(scriptCareer,0,sizeof scriptCareer);
    memset(scriptCareerSaveError,0,sizeof scriptCareerSaveError);
    if(cfg) for(unsigned i=0;i<2;++i) (void)newest(cfg,i,&scriptCareer[i]);
}
bool scriptCareerStore(const char *cfg,unsigned lang)
{
    if(lang>1 || !cfg) return false;
    ScriptCareer old, next=scriptCareer[lang];
    int slot=newest(cfg,lang,&old);
    next.serial=slot<0?1:old.serial+1;
    unsigned target=slot==0?1:0;
    char path[CAREER_PATH];uint8_t data[SCRIPT_CAREER_BYTES];
    bool ok=pathFor(cfg,lang,target,path) && scriptCareerEncode(lang,&next,data);
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
    if(lang>1 || !shelf || shelf>999 || revealed>peels) return;
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
