#include "cjk_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const CjWord words[]={{{0x4e00,0x4e8c},2,"yi1 er4","one two"},{{0x4e09,0x56db},2,"san1 si4","three four"},{{0x4e00,0x4e8c,0x4e09},3,"yi1 er4 san1","one two three"}};
static const CjLexicon lex={words,sizeof words/sizeof words[0]};
static void setup(CjGame *g,int mode,unsigned shelf){
    assert(cjStartMode(g,&lex,7,mode,shelf,40));
}
static void chain(CjGame *g,int a,int b){
    g->len=0;g->tile[a]=words[0].text[0];g->tile[b]=words[0].text[1];
    assert(cjAdd(g,a)&&cjAdd(g,b));
}
int main(void){
    CjGame g,snap;
    setup(&g,CJ_FIRE_NORMAL,1);
    assert(g.heatLimit==10&&g.burnEvery==4&&g.score==40);
    assert(cjHeatCost(&g,3)==3&&cjWordHeat(2)==3&&cjWordHeat(6)==0);
    for(int i=0;i<4;i++){chain(&g,0,1);assert(cjSubmit(&g));}
    unsigned flames=0;for(int i=0;i<CJ_CELLS;i++)flames+=g.flame[i]!=0;
    assert(flames==1&&g.turns==4&&g.heat==0);
    setup(&g,CJ_FIRE_NORMAL,1);g.flame[2]=1;g.heat=4;
    snap=g;assert(cjPeel(&g,2)==-2&&cjPeel(&g,3)==-2&&!memcmp(&g,&snap,sizeof g));
    chain(&g,2,3);assert(cjSubmit(&g)&&!g.flame[2]&&g.heat==5&&g.score==84);
    setup(&g,CJ_FIRE_NORMAL,1);g.flame[0]=1;g.opened[1]=1;g.back[1]=0;
    g.burnTick=g.burnEvery-1;chain(&g,12,13);assert(cjSubmit(&g));
    assert(!g.flame[0]&&g.flame[2]);
    setup(&g,CJ_FIRE_NORMAL,1);g.flame[0]=1;g.opened[1]=1;g.back[1]=1;
    g.burnTick=g.burnEvery-1;chain(&g,12,13);assert(cjSubmit(&g));assert(g.flame[0]&&!g.flame[1]);
    setup(&g,CJ_FIRE_NORMAL,1);g.flame[CJ_CELLS-1]=1;g.burnTick=g.burnEvery-1;
    g.back[0]=0;assert(cjPeel(&g,0)==0&&g.loss==CJ_LOSS_FIRE);
    snap=g;assert(cjEnded(&g)&&!cjAdd(&g,1)&&!cjHint(&g)&&!cjRecover(&g)&&!cjSubmit(&g)&&cjPeel(&g,1)==-1&&!memcmp(&g,&snap,sizeof g));
    assert(cjRetryShelf(&g));CjGame fresh;setup(&fresh,CJ_FIRE_NORMAL,1);assert(!memcmp(&g,&fresh,sizeof g));
    setup(&g,CJ_FIRE_NORMAL,8);assert(g.heatLimit==8&&g.burnEvery==3);
    g.passLen=2;g.foundMask=1;g.back[0]=2;g.flame[CJ_CELLS-1]=1;g.burnTick=g.burnEvery-1;
    assert(cjPeel(&g,0)==2&&cjComplete(&g)&&!g.loss);
    uint32_t banked=g.score;assert(banked>40&&cjNextShelf(&g)&&g.shelf==9&&g.score==banked&&!g.turns&&!g.heat&&!g.loss);
    for(int i=0;i<CJ_CELLS;i++)assert(!g.flame[i]);
    setup(&g,CJ_FIRE_OFF,1);for(int i=0;i<30;i++){chain(&g,0,1);assert(cjSubmit(&g));}assert(!g.heat&&!g.loss&&!g.heatLimit);
    setup(&g,CJ_FIRE_FAST,1);assert(g.heatLimit==8&&g.burnEvery==2);
    setup(&g,CJ_FIRE_RELAXED,1);assert(g.heatLimit==14&&g.burnEvery==5);
    setup(&g,CJ_FIRE_NORMAL,1);for(int i=0;i<CJ_CELLS;i++)g.tile[i]=0x3001;
    g.tile[0]=g.tile[28]=words[0].text[0];g.tile[1]=g.tile[29]=words[0].text[1];g.flame[29]=1;
    assert(cjHint(&g)&&g.len==2&&g.chain[1]==29);g.flame[35]=1;
    assert(cjHint(&g)&&cjSelected(&g)>=0);
    setup(&g,CJ_FIRE_NORMAL,1);for(int i=0;i<CJ_CELLS;i++)g.tile[i]=0x3001;
    snap=g;assert(cjRecover(&g)&&g.turns==1&&g.heat==2&&cjSelected(&g)>=0);
    assert(!memcmp(g.back,snap.back,sizeof g.back)&&!memcmp(g.pass,snap.pass,sizeof g.pass)&&g.score==snap.score);
    puts("CJK FIRE OK: heat, extinguishing, flame movement, blocked peels, terminal loss, retry, progression and recovery");
}
