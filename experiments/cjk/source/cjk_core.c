#include "cjk_core.h"
#include <string.h>
static bool cellOK(int i){return i>=0 && i<CJ_CELLS;}
// Ascending neighbor order matches the original all-cell scan. The sentinel
// permits fast, bounded traversal without division or a per-node 36-cell scan.
static const uint8_t neighbors[CJ_CELLS][7]={
    {1,6,255,255,255,255,255},
    {0,2,6,7,255,255,255},
    {1,3,7,8,255,255,255},
    {2,4,8,9,255,255,255},
    {3,5,9,10,255,255,255},
    {4,10,11,255,255,255,255},
    {0,1,7,12,13,255,255},
    {1,2,6,8,13,14,255},
    {2,3,7,9,14,15,255},
    {3,4,8,10,15,16,255},
    {4,5,9,11,16,17,255},
    {5,10,17,255,255,255,255},
    {6,13,18,255,255,255,255},
    {6,7,12,14,18,19,255},
    {7,8,13,15,19,20,255},
    {8,9,14,16,20,21,255},
    {9,10,15,17,21,22,255},
    {10,11,16,22,23,255,255},
    {12,13,19,24,25,255,255},
    {13,14,18,20,25,26,255},
    {14,15,19,21,26,27,255},
    {15,16,20,22,27,28,255},
    {16,17,21,23,28,29,255},
    {17,22,29,255,255,255,255},
    {18,25,30,255,255,255,255},
    {18,19,24,26,30,31,255},
    {19,20,25,27,31,32,255},
    {20,21,26,28,32,33,255},
    {21,22,27,29,33,34,255},
    {22,23,28,34,35,255,255},
    {24,25,31,255,255,255,255},
    {25,26,30,32,255,255,255},
    {26,27,31,33,255,255,255},
    {27,28,32,34,255,255,255},
    {28,29,33,35,255,255,255},
    {29,34,255,255,255,255,255},
};
bool cjAdjacent(int a,int b){
    if(!cellOK(a)||!cellOK(b))return false;
    for(const uint8_t *p=neighbors[a];*p!=255;p++)if(*p==b)return true;
    return false;
}
static uint32_t next(CjGame *g){g->rng=g->rng*1664525u+1013904223u;return g->rng;}
static uint16_t randomChar(CjGame *g){
    const CjWord *w=&g->lex->words[(next(g)>>8)%g->lex->count];
    return w->text[(next(g)>>8)%w->len];
}
static bool tileOK(const CjGame *g,int i){return cellOK(i)&&(!g->opened[i]||g->back[i]);}
int cjLookup(const CjLexicon *lex,const uint16_t *text,int len){
    if(!lex||!lex->words||!text||len<2||len>CJ_MAX)return -1;
    for(unsigned i=0;i<lex->count;i++)
        if(lex->words[i].len==len&&!memcmp(text,lex->words[i].text,(size_t)len*2))return (int)i;
    return -1;
}
bool cjStartMode(CjGame *g,const CjLexicon *lex,uint32_t seed,int mode,unsigned shelf,uint32_t score){
    if(!g||!lex||!lex->words||!lex->count||mode<CJ_FIRE_OFF||mode>CJ_FIRE_FAST||!shelf||shelf>999)return false;
    for(unsigned i=0;i<lex->count;i++)
        if(lex->words[i].len<2||lex->words[i].len>CJ_MAX)return false;
    memset(g,0,sizeof *g);
    g->lex=lex;g->rng=g->dealSeed=seed;g->score=g->bankedScore=score;
    g->shelf=(uint16_t)shelf;g->fireMode=(int8_t)mode;
    g->focus=-1;g->lastWord=UINT16_MAX;
    if(mode!=CJ_FIRE_OFF){
        unsigned step=shelf>=16?2:shelf>=8?1:0;
        g->heatLimit=(uint8_t)((mode==CJ_FIRE_RELAXED?14:mode==CJ_FIRE_FAST?8:10)-2*step);
        unsigned cadence=mode==CJ_FIRE_RELAXED?5:mode==CJ_FIRE_FAST?2:4;
        g->burnEvery=(uint8_t)(cadence>step?cadence-step:1);
    }
    unsigned password=(next(g)>>8)%lex->count;
    const CjWord *pass=&lex->words[password];
    g->passLen=pass->len;
    memcpy(g->pass,pass->text,(size_t)pass->len*2);
    for(int r=0;r<CJ_ROWS;r++){
        unsigned i=(next(g)>>8)%lex->count;
        if(lex->count>1&&i==password)i=(i+1)%lex->count;
        const CjWord *w=&lex->words[i];
        int begin=(int)((next(g)>>8)%(CJ_COLS-w->len+1));
        for(int c=0;c<CJ_COLS;c++)
            g->tile[c*CJ_ROWS+r]=(c>=begin&&c<begin+w->len)?w->text[c-begin]:randomChar(g);
    }
    // Sample positions without replacement, with a hard bounded loop.
    uint8_t order[CJ_CELLS];for(int i=0;i<CJ_CELLS;i++)order[i]=(uint8_t)i;
    for(int i=0;i<g->passLen;i++){
        int j=i+(int)((next(g)>>8)%(CJ_CELLS-i));
        uint8_t tmp=order[i];order[i]=order[j];order[j]=tmp;
        g->back[order[i]]=(uint8_t)(i+1);
    }
    return true;
}
bool cjStart(CjGame *g,const CjLexicon *lex,uint32_t seed){
    return cjStartMode(g,lex,seed,CJ_FIRE_OFF,1,0);
}
bool cjComplete(const CjGame *g){
    return g&&g->passLen&&g->foundMask==((1u<<g->passLen)-1u);
}
bool cjEnded(const CjGame *g){return g&&(g->loss||cjComplete(g));}
bool cjNextShelf(CjGame *g){
    return g&&cjComplete(g)&&!g->loss&&g->shelf<999&&
        cjStartMode(g,g->lex,g->dealSeed+0x9e3779b9u,g->fireMode,g->shelf+1u,g->score);
}
bool cjRetryShelf(CjGame *g){
    return g&&g->loss&&cjStartMode(g,g->lex,g->dealSeed,g->fireMode,g->shelf,g->bankedScore);
}
int cjWordHeat(int len){return len<2?0:len==2?3:len==3?2:len==4?1:0;}
int cjHeatCost(const CjGame *g,int base){
    if(!g||!g->heatLimit)return 0;
    unsigned late=g->turns>=12?1+(g->turns-12)/6:0;
    return base+(int)(late>8?8:late);
}
bool cjPeelBlocked(const CjGame *g,int cell){
    return g&&cellOK(cell)&&!g->opened[cell]&&
        (g->flame[cell]||(cell%CJ_ROWS&&g->flame[cell-1]));
}
// Fire is an overlay on the character tiles. Movement uses only public
// tile geometry; secret coordinates and characters never affect ignition.
static bool fireStep(CjGame *g){
    for(int c=0;c<CJ_COLS;c++)if(g->flame[c*CJ_ROWS+CJ_ROWS-1])return false;
    for(int c=0;c<CJ_COLS;c++)for(int r=CJ_ROWS-2;r>=0;r--){
        int p=c*CJ_ROWS+r;
        if(!g->flame[p])continue;
        int q=p+1,end=(c+1)*CJ_ROWS;
        while(q<end&&g->opened[q]&&!g->back[q])q++;
        if(q==end)return false;
        if(g->opened[q]||g->flame[q])continue;
        g->flame[p]=0;g->flame[q]=1;
    }
    return true;
}
static bool ignite(CjGame *g){
    unsigned start=(next(g)>>8)%CJ_COLS;
    for(int pass=0;pass<2;pass++)for(unsigned k=0;k<CJ_COLS;k++){
        int col=(int)((start+k)%CJ_COLS),first=-1;bool burning=false;
        for(int r=0;r<CJ_ROWS;r++){
            int p=col*CJ_ROWS+r;
            burning|=g->flame[p]!=0;
            if(first<0&&!g->opened[p]&&!g->flame[p])first=p;
        }
        if(first>=0&&(pass||!burning)){g->flame[first]=1;return true;}
    }
    return false;
}
static void spendTurn(CjGame *g,int base,int cleared){
    int cost=cjHeatCost(g,base);g->turns++;
    if(!g->heatLimit)return;
    if(cjComplete(g)){
        unsigned speed=g->turns<16?16-g->turns:0;
        g->score+=100u*g->shelf+10u*speed;return;
    }
    int heat=(int)g->heat+cost-2*cleared;
    g->heat=(uint8_t)(heat>0?heat:0);
    if(++g->burnTick>=g->burnEvery){
        g->burnTick=0;
        if(!fireStep(g)){g->loss=CJ_LOSS_FIRE;g->len=0;return;}
    }
    if(g->heat>=g->heatLimit){
        if(ignite(g))g->heat=0;
        else g->heat=g->heatLimit-1;
    }
}
bool cjAdd(CjGame *g,int cell){
    if(!g||cjEnded(g)||!tileOK(g,cell))return false;
    for(unsigned i=0;i<g->len;i++)if(g->chain[i]==cell){
        if(i+1==g->len)return false;
        g->len=(uint8_t)(i+1);return true;
    }
    if(g->len==CJ_MAX||(g->len&&!cjAdjacent(g->chain[g->len-1],cell)))return false;
    g->chain[g->len++]=(uint8_t)cell;return true;
}
int cjSelected(const CjGame *g){
    if(!g||cjEnded(g)||g->len<2||g->len>CJ_MAX)return -1;
    uint16_t word[CJ_MAX];uint64_t used=0;
    for(unsigned i=0;i<g->len;i++){
        int p=g->chain[i];if(!tileOK(g,p)||(used&(UINT64_C(1)<<p))||(i&&!cjAdjacent(g->chain[i-1],p)))return -1;
        used|=UINT64_C(1)<<p;word[i]=g->tile[p];
    }
    return cjLookup(g->lex,word,g->len);
}
bool cjSubmit(CjGame *g){
    int index=cjSelected(g);if(index<0)return false;
    int n=g->len;g->focus=(int8_t)g->chain[n-1];
    g->surveyed[(unsigned)g->focus]=1;
    int cleared=0;
    for(int i=0;i<n;i++){
        int p=g->chain[i];cleared+=g->flame[p]!=0;g->flame[p]=0;
        if(!g->opened[p])g->tile[p]=randomChar(g);
    }
    g->score+=(uint32_t)(10*n+n*n+20*cleared);g->words++;g->lastWord=(uint16_t)index;g->len=0;
    spendTurn(g,cjWordHeat(n),cleared);
    return true;
}
int cjClue(const CjGame *g,int cell){
    if(!g||!cellOK(cell)||(!g->opened[cell]&&!g->surveyed[cell]))return -1;
    int n=0;for(const uint8_t *p=neighbors[cell];*p!=255;p++)n+=g->back[*p]!=0;
    return n;
}
bool cjEvidence(const CjGame *g,int cell,int *remaining,int *covered){
    if(!remaining||!covered||cjClue(g,cell)<0)return false;
    int left=0,unknown=0;
    for(const uint8_t *p=neighbors[cell];*p!=255;p++)if(!g->opened[*p]){
        unknown++;left+=g->back[*p]!=0;
    }
    *remaining=left;*covered=unknown;return true;
}
int cjPeel(CjGame *g,int cell){
    if(!g||!cellOK(cell)||cjEnded(g))return -1;
    if(g->opened[cell]){g->focus=(int8_t)cell;return -1;}
    if(cjPeelBlocked(g,cell))return -2;
    g->opened[cell]=1;g->focus=(int8_t)cell;g->len=0;
    int index=g->back[cell];
    if(index){int p=index-1;g->foundMask|=(uint8_t)(1u<<p);g->tile[cell]=g->pass[p];}
    spendTurn(g,1,0);return index;
}
#ifdef CJ_HINT_TEST
unsigned cjHintVisits;
#define HINT_VISIT() (++cjHintVisits)
#else
#define HINT_VISIT() ((void)0)
#endif
static bool walk(const CjGame *g,const CjWord *w,int cell,int d,uint64_t used,
                 uint8_t path[CJ_MAX],unsigned *budget,int urgent,bool hitFire){
    if(!*budget||!tileOK(g,cell)||(used&(UINT64_C(1)<<cell)))return false;
    HINT_VISIT();--*budget;
    if(g->tile[cell]!=w->text[d])return false;
    hitFire|=g->flame[cell]&&cell%CJ_ROWS==urgent;
    int left=w->len-d-1,row=cell%CJ_ROWS;
    if(urgent>=0&&!hitFire&&(row+left<urgent||row-left>urgent))return false;
    path[d]=(uint8_t)cell;
    if(d+1==w->len)return urgent<0||hitFire;
    used|=UINT64_C(1)<<cell;
    for(const uint8_t *p=neighbors[cell];*p!=255;p++)
        if(walk(g,w,*p,d+1,used,path,budget,urgent,hitFire))return true;
    return false;
}
// Stack-only index of playable root tiles, ordered by codepoint then cell.
// Binary search rejects absent first characters without entering the DFS.
typedef struct { uint32_t key[CJ_CELLS]; unsigned count; } HintRoots;
static void rootsBuild(const CjGame *g,HintRoots *roots){
    roots->count=0;
    for(int p=0;p<CJ_CELLS;p++)if(tileOK(g,p)){
        uint32_t key=((uint32_t)g->tile[p]<<6)|(unsigned)p;
        unsigned j=roots->count++;
        while(j&&roots->key[j-1]>key){roots->key[j]=roots->key[j-1];j--;}
        roots->key[j]=key;
    }
}
static unsigned rootFirst(const HintRoots *roots,uint16_t cp){
    uint32_t key=(uint32_t)cp<<6;
    unsigned lo=0,hi=roots->count;
    while(lo<hi){unsigned mid=(lo+hi)/2;if(roots->key[mid]<key)lo=mid+1;else hi=mid;}
    return lo;
}
static bool findHint(CjGame *g,const HintRoots *roots,int urgent,unsigned budget){
    uint8_t path[CJ_MAX];
    for(unsigned w=0;w<g->lex->count&&budget;w++){
        const CjWord *word=&g->lex->words[w];
        uint16_t first=word->text[0];
        for(unsigned i=rootFirst(roots,first);i<roots->count&&budget&&(roots->key[i]>>6)==first;i++)
            if(walk(g,word,(int)(roots->key[i]&63),0,0,path,&budget,urgent,false)){
                g->len=word->len;memcpy(g->chain,path,g->len);return true;
            }
    }
    return false;
}
bool cjHint(CjGame *g){
#ifdef CJ_HINT_TEST
    cjHintVisits=0;
#endif
    if(!g||!g->lex||cjEnded(g))return false;
    HintRoots roots;rootsBuild(g,&roots);
    int urgent=-1;
    for(int i=0;i<CJ_CELLS;i++)if(g->flame[i]&&i%CJ_ROWS>urgent)urgent=i%CJ_ROWS;
    if(urgent>=0&&findHint(g,&roots,urgent,12000))return true;
    return findHint(g,&roots,-1,24000);
}
int cjTrainingStep(const CjGame *g){
    if(!g||!g->lex||g->heatLimit||cjEnded(g))return 0;
    if(!g->words)return 1;
    unsigned opened=0;for(int i=0;i<CJ_CELLS;i++)opened+=g->opened[i]!=0;
    return opened==0?2:opened==1?3:4;
}
unsigned cjDailyVocabulary(unsigned count,int date){
    return date>0&&date<=20261127&&count>132?132:count;
}
bool cjRecover(CjGame *g){
    if(!g||!g->lex||!g->lex->words||!g->lex->count||cjEnded(g))return false;
    CjGame probe=*g;
    if(cjHint(&probe))return false;
    // Pick only from public unopened geometry. Never use hidden positions
    // or password characters to decide which cells or word to plant.
    int a=-1,b=-1;
    unsigned first=(next(&probe)>>8)%CJ_CELLS;
    for(unsigned k=0;k<CJ_CELLS&&a<0;k++){
        int i=(int)((first+k)%CJ_CELLS);
        if(g->opened[i])continue;
        for(int j=0;j<CJ_CELLS;j++)if(!g->opened[j]&&cjAdjacent(i,j)){
            a=i;b=j;break;
        }
    }
    if(a<0)return false;
    unsigned start=(next(&probe)>>8)%g->lex->count;
    for(unsigned k=0;k<g->lex->count;k++){
        const CjWord *w=&g->lex->words[(start+k)%g->lex->count];
        if(w->len!=2)continue;
        g->tile[a]=w->text[0];g->tile[b]=w->text[1];
        g->chain[0]=(uint8_t)a;g->chain[1]=(uint8_t)b;g->len=2;
        g->rng=probe.rng;spendTurn(g,2,0);
        return true;
    }
    return false;
}
