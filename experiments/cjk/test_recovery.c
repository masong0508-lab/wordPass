#include "cjk_core.h"
#include "words_generated.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern bool cjRecover(CjGame *g);
int main(void){
    unsigned tested=0;
    for(int lang=0;lang<3;lang++){
        const CjLexicon *lex=&lexicons[lang];
        for(unsigned seed=1;seed<=128;seed++){
            CjGame g,before,repeat;
            assert(cjStart(&g,lex,seed));
            before=g;
            assert(!cjRecover(&g)&&!memcmp(&g,&before,sizeof g));
            for(int i=0;i<CJ_CELLS;i++)g.tile[i]=0x3001;
            g.opened[0]=1;g.back[0]=0;g.surveyed[1]=1;
            before=repeat=g;
            assert(!cjHint(&g));
            assert(cjRecover(&g)&&cjRecover(&repeat));
            assert(!memcmp(&g,&repeat,sizeof g));
            assert(g.turns==before.turns+1&&g.score==before.score);
            assert(g.len==2&&cjSelected(&g)>=0);
            assert(!memcmp(g.pass,before.pass,sizeof g.pass)&&g.foundMask==before.foundMask);
            assert(!memcmp(g.back,before.back,sizeof g.back));
            assert(!memcmp(g.opened,before.opened,sizeof g.opened));
            assert(!memcmp(g.surveyed,before.surveyed,sizeof g.surveyed));
            for(int i=0;i<CJ_CELLS;i++)if(i!=g.chain[0]&&i!=g.chain[1])assert(g.tile[i]==before.tile[i]);
            assert(!g.opened[g.chain[0]]&&!g.opened[g.chain[1]]);
            before=g;assert(!cjRecover(&g)&&!memcmp(&g,&before,sizeof g));
            assert(cjSubmit(&g));
            for(int i=0;i<CJ_CELLS;i++){g.opened[i]=1;g.tile[i]=0x3001;}
            g.opened[0]=0;g.opened[CJ_CELLS-1]=0;g.len=0;
            before=g;assert(!cjRecover(&g)&&!memcmp(&g,&before,sizeof g));
            tested++;
        }
    }
    printf("CJK RECOVERY OK: %u seeded boards, refusal, deterministic recovery and unchanged hidden state\n",tested);
}
