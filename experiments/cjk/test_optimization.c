#include "cjk_core.h"
#include "words_generated.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
// Independent reference: original 36-cell neighbor scans and dictionary
// root order, without the optimization. Count actual playable DFS visits.
static unsigned oldVisits;
static bool adj(int a,int b){
    int dc=b/CJ_ROWS-a/CJ_ROWS,dr=b%CJ_ROWS-a%CJ_ROWS;
    return dc==0?(dr==1||dr==-1):((dc==1||dc==-1)&&(dr==0||dr==((a/CJ_ROWS&1)?1:-1)));
}
static bool oldWalk(const CjGame *g,const CjWord *w,int cell,int depth,
                   uint64_t used,uint8_t path[CJ_MAX],int urgent,bool hit){
    if((g->opened[cell]&&!g->back[cell])||(used&(UINT64_C(1)<<cell)))return false;
    oldVisits++;
    if(g->tile[cell]!=w->text[depth])return false;
    hit|=g->flame[cell]&&cell%CJ_ROWS==urgent;
    int left=w->len-depth-1,row=cell%CJ_ROWS;
    if(urgent>=0&&!hit&&(row+left<urgent||row-left>urgent))return false;
    path[depth]=(uint8_t)cell;
    if(depth+1==w->len)return urgent<0||hit;
    used|=UINT64_C(1)<<cell;
    for(int i=0;i<CJ_CELLS;i++)if(adj(cell,i)&&oldWalk(g,w,i,depth+1,used,path,urgent,hit))return true;
    return false;
}
static bool reference(CjGame *g,int urgent){
    uint8_t path[CJ_MAX];
    for(unsigned w=0;w<g->lex->count;w++)for(int p=0;p<CJ_CELLS;p++)
        if(oldWalk(g,&g->lex->words[w],p,0,0,path,urgent,false)){
            g->len=g->lex->words[w].len;memcpy(g->chain,path,g->len);return true;
        }
    return false;
}
int main(void){
    unsigned checked=0;unsigned long long former=0,optimized=0;
    for(int a=0;a<CJ_CELLS;a++)for(int b=0;b<CJ_CELLS;b++)assert(cjAdjacent(a,b)==adj(a,b));
    for(int lang=0;lang<3;lang++){
        const CjLexicon *lex=&lexicons[lang];assert(lex->count>=252);
        assert(cjDailyVocabulary(lex->count,20261127)==132);
        assert(cjDailyVocabulary(lex->count,20261128)==lex->count);
        assert(cjDailyVocabulary(lex->count,0)==lex->count);
        for(unsigned seed=1;seed<=256;seed++){
            CjGame g,old,before;
            assert(cjStartMode(&g,lex,seed,CJ_FIRE_NORMAL,1,0));
            if(seed&1)g.flame[(seed*17)%CJ_CELLS]=1;
            before=old=g;
            oldVisits=0;
            int urgent=-1;for(int i=0;i<CJ_CELLS;i++)if(g.flame[i]&&i%CJ_ROWS>urgent)urgent=i%CJ_ROWS;
            bool expected=urgent>=0&&reference(&old,urgent);
            if(!expected)expected=reference(&old,-1);
            bool actual=cjHint(&g);
            assert(actual&&expected&&cjSelected(&g)>=0);
            assert(g.len==old.len&&!memcmp(g.chain,old.chain,g.len));
            former+=oldVisits;optimized+=cjHintVisits;
            g.len=before.len;memcpy(g.chain,before.chain,sizeof g.chain);
            assert(!memcmp(&g,&before,sizeof g));
            checked++;
        }
        CjGame lesson;assert(cjStart(&lesson,lex,31)&&cjTrainingStep(&lesson)==1);
        assert(cjHint(&lesson)&&cjSubmit(&lesson)&&cjTrainingStep(&lesson)==2);
        int a=-1,b=-1;for(int i=0;i<CJ_CELLS;i++)if(!lesson.back[i]){if(a<0)a=i;else{b=i;break;}}
        assert(a>=0&&b>=0&&cjPeel(&lesson,a)==0&&cjTrainingStep(&lesson)==3);
        assert(cjPeel(&lesson,b)==0&&cjTrainingStep(&lesson)==4);
        assert(cjStartMode(&lesson,lex,31,CJ_FIRE_NORMAL,1,0)&&cjTrainingStep(&lesson)==0);
    }
    assert(optimized<former/2);
    printf("CJK OPTIMIZATION OK: %u identical hints; %llu old vs %llu new DFS visits (not FPS); tutorial and legacy date checks\n",checked,former,optimized);
}
