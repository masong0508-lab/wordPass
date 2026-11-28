#include "cjk_core.h"
#include "extended_words.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    unsigned boards=0,turns=0;
    for(int l=0;l<4;l++){
        const CjLexicon *lex=&extraLexicons[l];assert(lex->count>=100);
        for(unsigned i=0;i<lex->count;i++){
            const CjWord *w=&lex->words[i];
            assert(cjLookup(lex,w->text,w->len)==(int)i);
            for(int j=0;j<w->len;j++){
                unsigned cp=w->text[j];
                assert(l==0?(cp>=0xac00&&cp<=0xd7a3):l==1?(cp>=0xe000&&cp<0xf000):cp>=0x61&&cp<=0x17f);
            }
        }
        for(unsigned seed=1;seed<=256;seed++){
            CjGame g,before;
            assert(cjStartMode(&g,lex,seed,CJ_FIRE_NORMAL,1,0));
            assert(cjHint(&g)&&cjSelected(&g)>=0);
            assert(cjSubmit(&g)&&g.words==1&&g.turns==1);
            for(int p=0;p<CJ_CELLS;p++)if(g.back[p]&&!cjEnded(&g)){
                assert(!cjPeelBlocked(&g,p));assert(cjPeel(&g,p)>0);
            }
            assert(cjComplete(&g)&&!g.loss);
            unsigned bank=g.score;
            assert(cjNextShelf(&g)&&g.shelf==2&&g.score==bank);
            before=g;
            assert(cjHint(&g)&&cjSelected(&g)>=0);
            assert(!memcmp(g.back,before.back,sizeof g.back));
            turns+=g.turns;boards++;
        }
    }
    printf("EXTENDED LANGUAGES OK: %u deterministic native boards, exact words, hints, safe reveals and next shelves\n",boards);
}
