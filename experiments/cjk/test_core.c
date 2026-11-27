#include "cjk_core.h"
#include "words_generated.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    unsigned boards=0;
    for(int lang=0;lang<3;lang++){
        const CjLexicon *lex=&lexicons[lang];
        assert(lex->count>=130);
        for(unsigned i=0;i<lex->count;i++){
            const CjWord *w=&lex->words[i];
            assert(cjLookup(lex,w->text,w->len)==(int)i);
            assert(w->text[0]>255);
        }
        for(unsigned seed=1;seed<=128;seed++){
            CjGame g,same;
            assert(cjStart(&g,lex,seed)&&cjStart(&same,lex,seed));
            assert(!memcmp(&g,&same,sizeof g));
            unsigned hidden=0;int blank=-1;
            for(int p=0;p<CJ_CELLS;p++){
                hidden+=g.back[p]!=0;
                if(!g.back[p])blank=p;
                assert(cjClue(&g,p)==-1);
            }
            assert(hidden==g.passLen&&blank>=0);
            assert(cjHint(&g)&&cjSelected(&g)>=0);
            assert(!memcmp(g.tile,same.tile,sizeof g.tile)&&g.turns==0);
            assert(cjSubmit(&g)&&g.turns==1&&g.score>0);
            assert(cjClue(&g,g.focus)>=0);
            int left,covered,expectLeft=0,expectCovered=0;
            for(int p=0;p<CJ_CELLS;p++)if(cjAdjacent(g.focus,p)){
                expectCovered++;expectLeft+=g.back[p]!=0;
            }
            CjGame snapshot=g;
            assert(cjEvidence(&g,g.focus,&left,&covered));
            assert(left==expectLeft&&covered==expectCovered&&!memcmp(&g,&snapshot,sizeof g));
            assert(cjPeel(&g,blank)==0&&g.turns==2);
            assert(cjPeel(&g,blank)==-1&&g.turns==2);
            for(int p=0;p<CJ_CELLS;p++)if(g.back[p])assert(cjPeel(&g,p)>0);
            assert(cjComplete(&g)&&g.turns==(unsigned)g.passLen+2);
            boards++;
        }
        const CjWord *w=&lex->words[0];assert(w->len==2);
        CjGame g;assert(cjStart(&g,lex,19));
        g.tile[0]=w->text[0];g.tile[CJ_ROWS]=w->text[1];
        assert(cjAdd(&g,0)&&!cjAdd(&g,35)&&cjAdd(&g,CJ_ROWS));
        assert(cjSelected(&g)==0&&cjSubmit(&g));
    }
    // Controlled visible clue: one secret and two covered neighbors.
    CjGame e={0};e.surveyed[0]=1;e.passLen=2;e.back[1]=1;
    int rem=-1,cov=-1;assert(cjEvidence(&e,0,&rem,&cov)&&rem==1&&cov==2);
    assert(!cjEvidence(&e,1,&rem,&cov));
    e.opened[1]=1;
    assert(cjEvidence(&e,0,&rem,&cov)&&rem==0&&cov==1);
    e.opened[CJ_ROWS]=1;
    assert(cjEvidence(&e,0,&rem,&cov)&&rem==0&&cov==0);
    printf("CJK CORE OK: %u deterministic boards, two-character words, hints, clues and complete passwords\n",boards);
    return 0;
}
