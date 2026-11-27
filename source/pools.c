// Themed pass-word pools (format WPP1, built by tools/gen_pools.py into data/pools.bin).
// Words of one length are stored back to back, so word idx of length len is len bytes at
// base + idx * len; nothing is copied until a pass is picked.
#include "game.h"
#include <string.h>
#include "pools_bin.h"

#define MAX_THEMES 48
#define NLEN (POOL_MAX_LEN - POOL_MIN_LEN + 1)

// The immutable file already contains each name and count. Cache only the
// start of each theme; at most five preceding length runs are summed when
// selecting a password. The active and English views remain independent.
typedef struct {
    int n;
    const u8 *image;
    u32 offset[MAX_THEMES];
} PoolSet;
static PoolSet enSet, altSet;
static const PoolSet *P = &enSet;
static const u8 *header(const PoolSet *s,int theme) { return s->image+8+20u*(unsigned)theme; }
static unsigned countAt(const u8 *h,int k) { return h[8+2*k] | ((unsigned)h[9+2*k]<<8); }
static bool poolsParse(PoolSet *s,const u8 *p,size_t bytes)
{
    s->n=0;s->image=NULL;
    const bool trusted=bytes==IMAGE_TRUSTED;
    if(!p||(!trusted&&bytes<8)||memcmp(p,"WPP1",4))return false;
    u32 fileThemes=(u32)p[4]|((u32)p[5]<<8)|((u32)p[6]<<16)|((u32)p[7]<<24);
    if(!trusted&&(fileThemes==0||fileThemes>4096||8+20*(size_t)fileThemes>bytes))return false;
    int n=fileThemes>MAX_THEMES?MAX_THEMES:(int)fileThemes;
    unsigned long long used=8+20*(unsigned long long)fileThemes;
    for(int t=0;t<n;t++){
        const u8 *h=p+8+20u*(unsigned)t;
        unsigned words=0;
        if(used>UINT32_MAX)return false;
        s->offset[t]=(u32)used;
        for(int k=0;k<NLEN;k++){
            unsigned count=countAt(h,k);
            words+=count;
            size_t span=(size_t)count*(POOL_MIN_LEN+k);
            if(!trusted&&(used>bytes||span>bytes-(size_t)used))return false;
            used+=span;
        }
        if(!trusted&&words==0)return false;
    }
    if(!trusted&&used!=bytes)return false;
    s->image=p;s->n=n;return true;
}
void poolsInit(void){poolsParse(&enSet,pools_bin,IMAGE_TRUSTED);P=&enSet;}
bool poolsSet(const u8 *p,size_t bytes){
    if(!p||p==pools_bin){P=&enSet;return true;}
    if(!poolsParse(&altSet,p,bytes)){P=&enSet;return false;}
    P=&altSet;return true;
}
int poolThemeCount(void){return P->n;}
static const char *nameAt(const PoolSet *s,int theme){
    return theme>=0&&theme<s->n?(const char *)header(s,theme):"";
}
const char *poolName(int theme){return nameAt(P,theme);}
const char *poolNameEn(int theme){return nameAt(&enSet,theme);}
static int countFor(const PoolSet *s,int theme,int len){
    if(theme<0||theme>=s->n||len<POOL_MIN_LEN||len>POOL_MAX_LEN)return 0;
    return (int)countAt(header(s,theme),len-POOL_MIN_LEN);
}
int poolCount(int theme,int len){return countFor(P,theme,len);}
static void wordAt(const PoolSet *s,int theme,int len,int idx,char *out){
    out[0]=0;
    if(idx<0||idx>=countFor(s,theme,len))return;
    const u8 *h=header(s,theme);size_t offset=s->offset[theme];
    for(int k=0;k<len-POOL_MIN_LEN;k++)offset+=(size_t)countAt(h,k)*(POOL_MIN_LEN+k);
    memcpy(out,s->image+offset+(size_t)idx*len,(size_t)len);out[len]=0;
}
void poolWord(int theme,int len,int idx,char *out){wordAt(P,theme,len,idx,out);}
void poolWordEn(int theme,int len,int idx,char *out){wordAt(&enSet,theme,len,idx,out);}
