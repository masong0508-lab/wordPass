#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "cjk_core.h"
#include "words_generated.h"
#include "glyphs_generated.h"

static u16 top[256*192], bottom[256*192];
static u16 *surface;
static CjGame g;
static int lang=-1, armed=-1;
static bool peek, dirty=true, noHint=false,recoveryBlocked=false;
static unsigned seed=1;
static const uint16_t titles[3][5]={{0x7b80,0x4f53,0x4e2d,0x6587,0},{0x7e41,0x9ad4,0x4e2d,0x6587,0},{0x65e5,0x672c,0x8a9e,0,0}};
#define RGB(r,g,b) ((u16)(0x8000|(r)|((g)<<5)|((b)<<10)))
#define BG RGB(2,3,5)
#define PANEL RGB(5,7,10)
#define WHITE RGB(31,31,31)
#define GOLD RGB(31,26,12)
#define TEAL RGB(8,29,25)
#define RED RGB(31,10,8)
static void rect(int x,int y,int w,int h,u16 color){
    for(int j=y;j<y+h;j++)if(j>=0&&j<192)
        for(int i=x;i<x+w;i++)if(i>=0&&i<256)surface[j*256+i]=color;
}
static void box(int x,int y,int w,int h,u16 fill,u16 edge){
    rect(x,y,w,h,edge);rect(x+1,y+1,w-2,h-2,fill);
}
static void glyph(int locale,int x,int y,uint16_t cp,u16 color){
    const CjGlyph *a=locale==0?glyph0:locale==1?glyph1:glyph2;
    unsigned n=locale==0?sizeof glyph0/sizeof *glyph0:locale==1?sizeof glyph1/sizeof *glyph1:sizeof glyph2/sizeof *glyph2;
    unsigned lo=0,hi=n;
    while(lo<hi){unsigned m=(lo+hi)/2;if(a[m].code<cp)lo=m+1;else hi=m;}
    if(lo==n||a[lo].code!=cp){box(x,y,16,16,BG,RED);return;}
    for(int r=0;r<16;r++)for(int c=0;c<16;c++)if(a[lo].rows[r]&(1u<<(15-c)))rect(x+c,y+r,1,1,color);
}
static void ascii(int x,int y,const char *s,u16 color){
    for(;*s&&x<248;s++,x+=8){
        unsigned ch=(unsigned char)*s;
        if(ch<32||ch>126)ch='?';
        for(int r=0;r<10;r++)for(int c=0;c<8;c++)if(ascii8[ch-32][r]&(1u<<(7-c)))rect(x+c,y+r,1,1,color);
    }
}
static void centered(int y,const char *s,u16 color){
    int x=128-(int)strlen(s)*4;if(x<0)x=0;ascii(x,y,s,color);
}
static int nativeWidth(int id){int n=0;while(cjk_ui[lang][id][n])n++;return n*16;}
static void nativeText(int x,int y,int id,u16 color){
    for(int i=0;cjk_ui[lang][id][i];i++)glyph(lang,x+i*16,y,cjk_ui[lang][id][i],color);
}
static void nativeCenter(int y,int id,u16 color){nativeText(128-nativeWidth(id)/2,y,id,color);}
static void nativeTitle(int locale,int x,int y,u16 color){
    for(int i=0;i<4&&titles[locale][i];i++)glyph(locale,x+i*17,y,titles[locale][i],color);
}
static void origin(int i,int *x,int *y){int c=i/CJ_ROWS,r=i%CJ_ROWS;*x=5+28*c;*y=10+25*r+((c&1)?12:0);}
static int hit(int x,int y){
    for(int i=0;i<CJ_CELLS;i++){int a,b;origin(i,&a,&b);if(x>=a&&x<a+26&&y>=b&&y<b+23)return i;}
    return -1;
}
static bool selected(int i){for(int k=0;k<g.len;k++)if(g.chain[k]==i)return true;return false;}
static void button(int y,int id,u16 col){
    box(178,y,74,24,PANEL,col);nativeText(215-nativeWidth(id)/2,y+4,id,col);
}
static void start(int locale){lang=locale;peek=false;armed=-1;noHint=recoveryBlocked=false;cjStart(&g,&lexicons[lang],seed++);}
static void hint(void){
    peek=false;armed=-1;recoveryBlocked=false;
    if(cjComplete(&g)){noHint=false;dirty=true;return;}
    if(noHint){
        noHint=false;
        if(!cjRecover(&g))recoveryBlocked=true;
    }else if(!cjHint(&g)){
        g.len=0;noHint=true;
    }
    dirty=true;
}
static void render(void){
    surface=top;rect(0,0,256,192,BG);
    box(4,4,248,184,BG,GOLD);
    centered(10,"WORDPASS - CJK PRACTICE",GOLD);
    if(lang<0){
        centered(50,"Select a writing system",WHITE);
        centered(75,"Chinese: Mandarin Pinyin",TEAL);
        centered(91,"Japanese: Kana / Kanji",TEAL);
        centered(122,"Beginner vocabulary preview",WHITE);
        centered(145,"No fire or saved records",WHITE);
        surface=bottom;rect(0,0,256,192,BG);
        const char *labels[]={"Simplified","Traditional","Japanese"};
        for(int i=0;i<3;i++){int y=15+i*54;box(12,y,232,44,PANEL,TEAL);nativeTitle(i,20,y+12,WHITE);ascii(112,y+16,labels[i],GOLD);}
        return;
    }
    nativeTitle(lang,96,27,TEAL);
    int found=0;for(int i=0;i<g.passLen;i++)found+=(g.foundMask>>i)&1u;
    int x=128-12*g.passLen;
    for(int i=0;i<g.passLen;i++){
        box(x+24*i,51,22,24,PANEL,GOLD);
        if((g.foundMask>>i)&1u)glyph(lang,x+24*i+3,55,g.pass[i],WHITE);
        else ascii(x+24*i+7,58,"?",GOLD);
    }
    char msg[48];
    box(54,77,148,3,PANEL,GOLD);rect(55,78,146*found/g.passLen,1,TEAL);
    nativeText(12,82,UI_PASS,TEAL);snprintf(msg,sizeof msg,"%d/%d",found,g.passLen);ascii(49,85,msg,WHITE);
    nativeText(115,82,UI_MOVES,TEAL);snprintf(msg,sizeof msg,"%lu",(unsigned long)g.turns);ascii(153,85,msg,WHITE);
    if(cjComplete(&g)){
        nativeCenter(108,UI_DONE,GOLD);nativeCenter(131,UI_NEXT_BOARD,WHITE);
    }else if(g.len){
        int idx=cjSelected(&g);
        for(int i=0;i<g.len;i++)glyph(lang,128-g.len*9+i*18,98,g.tile[g.chain[i]],idx>=0?GOLD:WHITE);
        if(idx>=0){const CjWord *w=&g.lex->words[idx];centered(120,w->reading,TEAL);centered(136,w->meaning,WHITE);}
        else nativeCenter(120,g.len<2?UI_LINK_TWO:UI_UNKNOWN,RED);
    }else if(noHint||recoveryBlocked){
        nativeCenter(105,recoveryBlocked?UI_RECOVERY_BLOCKED:UI_NO_HINT,RED);
        if(noHint)nativeCenter(128,UI_RECOVERY_COST,GOLD);
    }else if(g.lastWord!=UINT16_MAX){
        const CjWord *w=&g.lex->words[g.lastWord];
        centered(100,w->reading,TEAL);centered(116,w->meaning,WHITE);
    }else nativeCenter(105,UI_MAKE_WORD,WHITE);
    int remaining=0,covered=0;
    bool evidence=!cjComplete(&g)&&cjEvidence(&g,g.focus,&remaining,&covered);
    if(evidence){
        nativeText(16,151,UI_CLUE,GOLD);snprintf(msg,sizeof msg,"%d/%d",remaining,covered);ascii(168,154,msg,TEAL);
    }
    nativeCenter(171,peek?UI_OPEN_PROMPT:UI_WORD_PROMPT,WHITE);
    surface=bottom;rect(0,0,256,192,BG);
    for(int i=0;i<CJ_CELLS;i++){
        int a,b;origin(i,&a,&b);
        bool hole=g.opened[i]&&!g.back[i];
        u16 border=selected(i)?WHITE:g.focus==i?TEAL:GOLD;
        if(evidence&&!selected(i)&&!g.opened[i]&&cjAdjacent(g.focus,i))
            border=remaining==0?TEAL:remaining==covered?WHITE:RGB(11,19,29);
        box(a,b,26,23,hole?BG:PANEL,border);
        if(!hole)glyph(lang,a+5,b+3,g.tile[i],g.opened[i]?TEAL:WHITE);
        int clue=cjClue(&g,i);
        if(clue>=0){char digit[2]={(char)('0'+clue),0};rect(a,b,9,10,BG);ascii(a,b,digit,GOLD);}
    }
    snprintf(msg,sizeof msg,"%lu",(unsigned long)g.score);nativeText(182,4,UI_SCORE,TEAL);ascii(182,23,msg,GOLD);
    button(37,UI_ACCEPT,cjSelected(&g)>=0?GOLD:WHITE);
    button(65,UI_CLEAR,WHITE);button(93,noHint?UI_RECOVER:UI_HINT,noHint?GOLD:TEAL);
    button(121,peek?UI_CANCEL:UI_OPEN,peek?GOLD:WHITE);
    button(149,cjComplete(&g)?UI_NEXT:UI_MENU,WHITE);
    ascii(5,178,"B: MENU   R: HINT   L: OPEN",TEAL);
}
int main(void){
    videoSetMode(MODE_5_2D);videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);vramSetBankC(VRAM_C_SUB_BG);
    int mainBg=bgInit(3,BgType_Bmp16,BgSize_B16_256x256,0,0);
    int subBg=bgInitSub(3,BgType_Bmp16,BgSize_B16_256x256,0,0);
    lcdMainOnBottom();
    bool drag=false,suppress=false;
    while(1){
        scanKeys();u32 down=keysDown(),held=keysHeld(),up=keysUp();
        touchPosition t;touchRead(&t);
        if(lang<0){
            if(down&KEY_TOUCH){int choice=((int)t.py-15)/54;if(t.py>=15&&choice<3&&(t.py-15)%54<44){start(choice);dirty=true;suppress=true;}}
        }else{
            if(down&(KEY_B|KEY_START)){lang=-1;peek=false;armed=-1;drag=false;dirty=true;}
            else if(down&KEY_A){noHint=recoveryBlocked=false;armed=-1;drag=false;suppress=true;cjSubmit(&g);dirty=true;}
            else if(down&KEY_Y){noHint=recoveryBlocked=false;armed=-1;drag=false;suppress=true;g.len=0;dirty=true;}
            else if(down&KEY_R){drag=false;suppress=true;hint();}
            if((down&KEY_TOUCH)&&!(down&(KEY_A|KEY_B|KEY_START|KEY_Y|KEY_R))){
                suppress=false;drag=false;armed=-1;
                bool hintTap=t.px>=178&&t.px<252&&t.py>=93&&t.py<117;
                if(!hintTap){dirty|=noHint||recoveryBlocked;noHint=recoveryBlocked=false;}
                if(t.px>=178&&t.px<252){
                    if(t.py>=37&&t.py<61)cjSubmit(&g);
                    else if(t.py>=65&&t.py<89){g.len=0;peek=false;}
                    else if(t.py>=93&&t.py<117){hint();}
                    else if(t.py>=121&&t.py<145){peek=!peek;g.len=0;}
                    else if(t.py>=149&&t.py<173){if(cjComplete(&g))start(lang);else lang=-1;}
                    suppress=true;dirty=true;
                }else{
                    int i=hit(t.px,t.py);
                    if(i>=0){
                        if(peek||(held&KEY_L)){armed=i;}
                        else if((g.opened[i]&&!g.back[i])||(cjClue(&g,i)>=0&&(!g.len)&&
                                t.px<5+28*(i/CJ_ROWS)+9&&t.py<10+25*(i%CJ_ROWS)+((i/CJ_ROWS)&1?12:0)+10)){
                            g.focus=(int8_t)i;dirty=true;suppress=true;
                        }
                        else {drag=true;dirty|=cjAdd(&g,i);}
                    }
                }
            }
            if((held&KEY_TOUCH)&&!suppress){
                int i=hit(t.px,t.py);
                if(armed>=0&&i!=armed)armed=-1;
                else if(drag&&i>=0)dirty|=cjAdd(&g,i);
            }
            if(up&KEY_TOUCH){if(armed>=0){cjPeel(&g,armed);peek=false;dirty=true;}armed=-1;drag=false;suppress=false;}
        }
        if(dirty){render();swiWaitForVBlank();dmaCopy(bottom,bgGetGfxPtr(mainBg),sizeof bottom);dmaCopy(top,bgGetGfxPtr(subBg),sizeof top);dirty=false;}
        else swiWaitForVBlank();
    }
}
