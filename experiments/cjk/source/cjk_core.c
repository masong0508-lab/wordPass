#include "cjk_core.h"
#include <string.h>
static bool cellOK(int i){return i>=0 && i<CJ_CELLS;}
bool cjAdjacent(int a,int b){
    if(!cellOK(a)||!cellOK(b)||a==b)return false;
    int ac=a/CJ_ROWS,ar=a%CJ_ROWS,bc=b/CJ_ROWS,br=b%CJ_ROWS;
    int dc=bc-ac,dr=br-ar;
    return dc==0?(dr==1||dr==-1):((dc==1||dc==-1)&&(dr==0||dr==((ac&1)?1:-1)));
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
bool cjStart(CjGame *g,const CjLexicon *lex,uint32_t seed){
    if(!g||!lex||!lex->words||!lex->count)return false;
    for(unsigned i=0;i<lex->count;i++)
        if(lex->words[i].len<2||lex->words[i].len>CJ_MAX)return false;
    memset(g,0,sizeof *g);
    g->lex=lex;g->rng=seed;g->focus=-1;g->lastWord=UINT16_MAX;
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
bool cjComplete(const CjGame *g){
    return g&&g->passLen&&g->foundMask==((1u<<g->passLen)-1u);
}
bool cjAdd(CjGame *g,int cell){
    if(!g||cjComplete(g)||!tileOK(g,cell))return false;
    for(unsigned i=0;i<g->len;i++)if(g->chain[i]==cell){
        if(i+1==g->len)return false;
        g->len=(uint8_t)(i+1);return true;
    }
    if(g->len==CJ_MAX||(g->len&&!cjAdjacent(g->chain[g->len-1],cell)))return false;
    g->chain[g->len++]=(uint8_t)cell;return true;
}
int cjSelected(const CjGame *g){
    if(!g||g->len<2||g->len>CJ_MAX)return -1;
    uint16_t word[CJ_MAX];uint64_t used=0;
    for(unsigned i=0;i<g->len;i++){
        int p=g->chain[i];if(!tileOK(g,p)||(used&(UINT64_C(1)<<p))||(i&&!cjAdjacent(g->chain[i-1],p)))return -1;
        used|=UINT64_C(1)<<p;word[i]=g->tile[p];
    }
    return cjLookup(g->lex,word,g->len);
}
bool cjSubmit(CjGame *g){
    int index=cjSelected(g);if(index<0||cjComplete(g))return false;
    int n=g->len;g->focus=(int8_t)g->chain[n-1];
    g->surveyed[(unsigned)g->focus]=1;
    for(int i=0;i<n;i++){int p=g->chain[i];if(!g->opened[p])g->tile[p]=randomChar(g);}
    g->score+=(uint32_t)(10*n+n*n);g->turns++;g->lastWord=(uint16_t)index;g->len=0;
    return true;
}
int cjClue(const CjGame *g,int cell){
    if(!g||!cellOK(cell)||(!g->opened[cell]&&!g->surveyed[cell]))return -1;
    int n=0;for(int i=0;i<CJ_CELLS;i++)if(cjAdjacent(cell,i)&&g->back[i])n++;
    return n;
}
bool cjEvidence(const CjGame *g,int cell,int *remaining,int *covered){
    if(!remaining||!covered||cjClue(g,cell)<0)return false;
    int left=0,unknown=0;
    for(int i=0;i<CJ_CELLS;i++)if(cjAdjacent(cell,i)&&!g->opened[i]){
        unknown++;left+=g->back[i]!=0;
    }
    *remaining=left;*covered=unknown;return true;
}
int cjPeel(CjGame *g,int cell){
    if(!g||!cellOK(cell)||cjComplete(g))return -1;
    if(g->opened[cell]){g->focus=(int8_t)cell;return -1;}
    g->opened[cell]=1;g->focus=(int8_t)cell;g->len=0;g->turns++;
    if(!g->back[cell])return 0;
    int p=g->back[cell]-1;g->foundMask|=(uint8_t)(1u<<p);g->tile[cell]=g->pass[p];
    return p+1;
}
static bool walk(const CjGame *g,const CjWord *w,int cell,int d,uint64_t used,
                 uint8_t path[CJ_MAX],unsigned *budget){
    if(!*budget||!tileOK(g,cell)||(used&(UINT64_C(1)<<cell)))return false;
    --*budget;
    if(g->tile[cell]!=w->text[d])return false;
    path[d]=(uint8_t)cell;
    if(d+1==w->len)return true;
    used|=UINT64_C(1)<<cell;
    for(int i=0;i<CJ_CELLS;i++)
        if(cjAdjacent(cell,i)&&walk(g,w,i,d+1,used,path,budget))return true;
    return false;
}
bool cjHint(CjGame *g){
    if(!g||!g->lex||cjComplete(g))return false;
    uint8_t path[CJ_MAX];unsigned budget=24000;
    for(unsigned w=0;w<g->lex->count&&budget;w++)
        for(int p=0;p<CJ_CELLS&&budget;p++)
            if(walk(g,&g->lex->words[w],p,0,0,path,&budget)){
                g->len=g->lex->words[w].len;memcpy(g->chain,path,g->len);return true;
            }
    return false;
}

bool cjRecover(CjGame *g){
    if(!g||!g->lex||!g->lex->words||!g->lex->count||cjComplete(g))return false;
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
        g->rng=probe.rng;g->turns++;
        return true;
    }
    return false;
}
