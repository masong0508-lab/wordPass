#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "cjk_core.h"
#include "cjk_integrated.h"
#include "../../../source/script_career.h"
#include "cjk_ui_ids.h"
#include <stdlib.h>
typedef struct { uint16_t code,rows[16]; } CjGlyph;
_Static_assert(sizeof(CjGlyph)==34,"packed glyph stride");
unsigned char *cjkPack;
static CjWord *words;
static CjLexicon lex;
static const CjGlyph *glyphs;
static unsigned glyphCount;
static const uint16_t (*uiLabels)[15];
static const uint8_t (*ascii8)[10];
static bool available[CJ_LOCALES],dailyMode;
static const char *recordPath;
static int dailyDate;
int cjkDailyRanked;
static bool recorded;
static int clipLo,clipHi;
unsigned cjkEpoch;
static const char *const packPaths[CJ_LOCALES]={"nitro:/cjk/0.bin","nitro:/cjk/1.bin","nitro:/cjk/2.bin",
    "nitro:/cjk/3.bin","nitro:/cjk/4.bin","nitro:/cjk/5.bin","nitro:/cjk/6.bin"};
const char *cjkLocaleName(int locale){
    static const char *const names[CJ_LOCALES]={"ZH-HANS","ZH-HANT","JAPANESE","KOREAN","HINDI","GREEK","TURKISH"};
    return locale>=0&&locale<CJ_LOCALES?names[locale]:"LANGUAGE";
}
static unsigned rd16(const unsigned char *b){return b[0]|((unsigned)b[1]<<8);}
static uint32_t rd32(const unsigned char *b){return rd16(b)|((uint32_t)rd16(b+2)<<16);}
static uint32_t hash(const unsigned char *p,size_t n){
    uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;
}
void cjkProbe(void){
    for(int i=0;i<CJ_LOCALES;i++){
        FILE *f=fopen(packPaths[i],"rb");char magic[4];
        available[i]=f&&fread(magic,1,4,f)==4&&!memcmp(magic,"CJK1",4);
        if(f)fclose(f);
    }
}
bool cjkAvailable(int locale){return locale>=0&&locale<CJ_LOCALES&&available[locale];}

static u16 *surface;
CjGame cjkGame;
static int lang=-1, armed=-1;
static bool peek, dirty=true, noHint=false,recoveryBlocked=false,drag,suppress,peelBlocked;
static const uint16_t titles[3][5]={{0x7b80,0x4f53,0x4e2d,0x6587,0},{0x7e41,0x9ad4,0x4e2d,0x6587,0},{0x65e5,0x672c,0x8a9e,0,0}};
#define RGB(r,cjkGame,b) ((u16)(0x8000|(r)|((cjkGame)<<5)|((b)<<10)))
#define BG RGB(2,3,5)
#define PANEL RGB(5,7,10)
#define WHITE RGB(31,31,31)
#define GOLD RGB(31,26,12)
#define TEAL RGB(8,29,25)
#define RED RGB(31,10,8)
static void rect(int x,int y,int w,int h,u16 color){
    int a=y<clipLo?clipLo:y,b=y+h>clipHi?clipHi:y+h;
    int l=x<0?0:x,r=x+w>256?256:x+w;
    if(a>=b||l>=r)return;
    for(int j=a;j<b;j++){
        u16 *dst=surface+j*256+l;
        for(int i=l;i<r;i++)*dst++=color;
    }
}
static void box(int x,int y,int w,int h,u16 fill,u16 edge){
    rect(x,y,w,h,edge);rect(x+1,y+1,w-2,h-2,fill);
}
static void glyph(int locale,int x,int y,uint16_t cp,u16 color){
    (void)locale;const CjGlyph *a=glyphs;unsigned n=glyphCount;
    if(y+16<=clipLo||y>=clipHi)return;
    unsigned lo=0,hi=n;
    while(lo<hi){unsigned m=(lo+hi)/2;if(a[m].code<cp)lo=m+1;else hi=m;}
    if(lo==n||a[lo].code!=cp){box(x,y,16,16,BG,RED);return;}
    // Clip once per row, not once per painted pixel.
    int left=x<0?-x:0,right=x+16>256?256-x:16;
    if(left>=right)return;
    for(int r=0;r<16;r++){
        int yy=y+r;if(yy<clipLo||yy>=clipHi)continue;
        unsigned mask=a[lo].rows[r];if(!mask)continue;
        u16 *dst=surface+yy*256+x+left;
        for(int col=left;col<right;col++,dst++)if(mask&(1u<<(15-col)))*dst=color;
    }
}
static void ascii(int x,int y,const char *s,u16 color){
    if(y+10<=clipLo||y>=clipHi)return;
    for(;*s&&x<248;s++,x+=8){
        unsigned ch=(unsigned char)*s;
        if(ch<32||ch>126)ch='?';
        int left=x<0?-x:0,right=x+8>256?256-x:8;
        if(left>=right)continue;
        for(int r=0;r<10;r++){
            int yy=y+r;if(yy<clipLo||yy>=clipHi)continue;
            unsigned mask=ascii8[ch-32][r];if(!mask)continue;
            u16 *dst=surface+yy*256+x+left;
            for(int col=left;col<right;col++,dst++)if(mask&(1u<<(7-col)))*dst=color;
        }
    }
}
static void centered(int y,const char *s,u16 color){
    int x=128-(int)strlen(s)*4;if(x<0)x=0;ascii(x,y,s,color);
}
// Greek and Turkish glyphs are rasterized into at most 12 pixels of ink.
// Retain original spacing for Han, kana, Hangul and shaped Hindi tiles.
static int nativeStep(void){return lang>=5?12:16;}
static int nativeWidth(int id){int n=0;while(uiLabels[id][n])n++;return n*nativeStep();}
static void nativeText(int x,int y,int id,u16 color){
    int step=nativeStep();
    for(int i=0;uiLabels[id][i];i++)glyph(lang,x+i*step-(16-step)/2,y,uiLabels[id][i],color);
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
static int selectionOrder(int i){for(int k=0;k<cjkGame.len;k++)if(cjkGame.chain[k]==i)return k+1;return 0;}
static void button(int y,int id,u16 col){
    box(178,y,74,24,PANEL,col);nativeText(215-nativeWidth(id)/2,y+4,id,col);
}
// RU/UK remain record IDs 0/1; original CJK IDs 2/3/4 and new IDs 5..8 are independent.
static unsigned recordLanguage(void){return (unsigned)(lang+2);}
static void finishRecord(bool leaving){
    if(lang<0||!cjkGame.lex||!cjkGame.heatLimit||recorded)return;
    if(dailyMode){
        if(cjkDailyRanked!=1||(!leaving&&!cjEnded(&cjkGame)))return;
        recorded=true;cjkDailyRanked=2;
        (void)scriptDailyCareerFinish(recordPath,recordLanguage(),dailyDate,cjkGame.score);
    }else{
        if(!cjEnded(&cjkGame))return;
        recorded=true;
        unsigned peels=0,revealed=0;
        for(int i=0;i<CJ_CELLS;i++)if(cjkGame.opened[i]){peels++;revealed+=cjkGame.back[i]!=0;}
        scriptCareerFinish(recordPath,recordLanguage(),cjkGame.score,cjkGame.shelf,
                           cjkGame.words,peels,revealed,cjComplete(&cjkGame)&&!cjkGame.loss);
    }
}
static void resetInput(void){
    peek=false;armed=-1;drag=false;suppress=true;
    noHint=recoveryBlocked=peelBlocked=false;dirty=true;
}
static bool nextOrRetry(void){
    finishRecord(false);
    if(dailyMode)return false;
    bool ok=cjkGame.loss?cjRetryShelf(&cjkGame):cjNextShelf(&cjkGame);
    if(ok){recorded=false;resetInput();cjkEpoch++;}
    return ok;
}
static void hint(void){
    peek=false;armed=-1;recoveryBlocked=peelBlocked=false;
    if(cjEnded(&cjkGame)){noHint=false;dirty=true;return;}
    if(noHint){
        noHint=false;
        if(!cjRecover(&cjkGame))recoveryBlocked=true;
    }else if(!cjHint(&cjkGame)){
        cjkGame.len=0;noHint=true;
    }
    dirty=true;
}
void cjkDraw(u16 *target,bool upper,int y0,int y1){
    if(!cjkPack)return;
    surface=target;clipLo=y0;clipHi=y1;
    rect(0,0,256,192,BG);
    const int lesson=cjTrainingStep(&cjkGame);
    if(upper){
    box(4,4,248,184,BG,GOLD);
    centered(10,dailyMode?(cjkDailyRanked?"DAILY SCORED":"DAILY PRACTICE"):cjkGame.heatLimit?"WORDPASS - CLASSIC":"WORDPASS - PRACTICE",GOLD);
    if(lang<3)nativeTitle(lang,96,27,TEAL);else centered(30,cjkLocaleName(lang),TEAL);
    int found=0;for(int i=0;i<cjkGame.passLen;i++)found+=(cjkGame.foundMask>>i)&1u;
    int x=128-12*cjkGame.passLen;
    for(int i=0;i<cjkGame.passLen;i++){
        box(x+24*i,51,22,24,PANEL,GOLD);
        if((cjkGame.foundMask>>i)&1u)glyph(lang,x+24*i+3,55,cjkGame.pass[i],WHITE);
        else ascii(x+24*i+7,58,"?",GOLD);
    }
    char msg[48];
    nativeText(12,27,UI_SHELF,GOLD);
    sniprintf(msg,sizeof msg,"%u",(unsigned)cjkGame.shelf);ascii(48,30,msg,WHITE);
    if(cjkGame.heatLimit){
        nativeText(172,23,UI_HEAT,TEAL);
        sniprintf(msg,sizeof msg,"%u/%u",(unsigned)cjkGame.heat,(unsigned)cjkGame.heatLimit);
        ascii(208,26,msg,GOLD);
        box(174,42,70,4,PANEL,GOLD);
        int amount=68*cjkGame.heat/cjkGame.heatLimit;
        rect(175,43,amount>68?68:amount,2,cjkGame.heat+3>=cjkGame.heatLimit?RED:GOLD);
    }
    box(54,77,148,3,PANEL,GOLD);rect(55,78,146*found/cjkGame.passLen,1,TEAL);
    nativeText(12,82,UI_PASS,TEAL);sniprintf(msg,sizeof msg,"%d/%d",found,cjkGame.passLen);ascii(49,85,msg,WHITE);
    nativeText(115,82,UI_MOVES,TEAL);sniprintf(msg,sizeof msg,"%lu",(unsigned long)cjkGame.turns);ascii(153,85,msg,WHITE);
    if(cjkGame.loss){
        nativeCenter(108,UI_FIRE_LOST,RED);nativeCenter(131,dailyMode?UI_MENU:UI_RETRY,WHITE);
    }else if(cjComplete(&cjkGame)){
        nativeCenter(108,UI_DONE,GOLD);nativeCenter(131,dailyMode?UI_MENU:UI_NEXT_BOARD,WHITE);
    }else if(peelBlocked){
        nativeCenter(108,UI_FIRE_WARN,RED);
    }else if(cjkGame.len){
        int idx=cjSelected(&cjkGame);
        for(int i=0;i<cjkGame.len;i++)glyph(lang,128-cjkGame.len*9+i*18,98,cjkGame.tile[cjkGame.chain[i]],idx>=0?GOLD:WHITE);
        if(idx>=0){
            const CjWord *w=&cjkGame.lex->words[idx];centered(120,w->reading,TEAL);centered(136,w->meaning,WHITE);
            if(cjkGame.heatLimit){
                int delta=cjHeatCost(&cjkGame,cjWordHeat(cjkGame.len));
                for(int i=0;i<cjkGame.len;i++)if(cjkGame.flame[cjkGame.chain[i]])delta-=2;
                sniprintf(msg,sizeof msg,"%s%d",delta>=0?"+":"",delta);
                ascii(194,102,msg,delta>0?GOLD:TEAL);
            }
        }
        else nativeCenter(120,cjkGame.len<2?UI_LINK_TWO:UI_UNKNOWN,RED);
    }else if(noHint||recoveryBlocked){
        nativeCenter(105,recoveryBlocked?UI_RECOVERY_BLOCKED:UI_NO_HINT,RED);
        if(noHint){
            nativeCenter(128,UI_RECOVERY_COST,GOLD);
            if(cjkGame.heatLimit){sniprintf(msg,sizeof msg,"+%d",cjHeatCost(&cjkGame,2));ascii(218,143,msg,GOLD);}
        }
    }else if(cjkGame.lastWord!=UINT16_MAX){
        const CjWord *w=&cjkGame.lex->words[cjkGame.lastWord];
        centered(100,w->reading,TEAL);centered(116,w->meaning,WHITE);
    }else nativeCenter(105,UI_MAKE_WORD,WHITE);
    int remaining=0,covered=0;
    bool evidence=!cjEnded(&cjkGame)&&cjEvidence(&cjkGame,cjkGame.focus,&remaining,&covered);
    if(evidence){
        nativeText(16,151,UI_CLUE,GOLD);sniprintf(msg,sizeof msg,"%d/%d",remaining,covered);ascii(168,154,msg,TEAL);
    }
    int lessonPrompt=lesson==1?UI_TRAIN_WORD:lesson==2?UI_TRAIN_OPEN:lesson==3?UI_TRAIN_CLUE:UI_TRAIN_PASS;
    nativeCenter(171,peek?UI_OPEN_PROMPT:lesson?lessonPrompt:UI_WORD_PROMPT,lesson?GOLD:WHITE);
    return;
    }
    int remaining=0,covered=0;
    bool evidence=!cjEnded(&cjkGame)&&cjEvidence(&cjkGame,cjkGame.focus,&remaining,&covered);
    char msg[48];
    for(int i=0;i<CJ_CELLS;i++){
        int a,b;origin(i,&a,&b);
        bool hole=cjkGame.opened[i]&&!cjkGame.back[i];
        int order=selectionOrder(i);
        u16 border=order?WHITE:cjkGame.focus==i?TEAL:GOLD;
        if(evidence&&!order&&!cjkGame.opened[i]&&cjAdjacent(cjkGame.focus,i))
            border=remaining==0?TEAL:remaining==covered?WHITE:RGB(11,19,29);
        if(cjkGame.flame[i]||(peek&&cjPeelBlocked(&cjkGame,i)))border=RED;
        box(a,b,26,23,hole?BG:cjkGame.flame[i]?RGB(13,4,3):PANEL,border);
        if(!hole)glyph(lang,a+5,b+3,cjkGame.tile[i],cjkGame.opened[i]?TEAL:WHITE);
        if(cjkGame.flame[i]){rect(a+3,b+19,20,2,RED);rect(a+8,b+17,10,2,GOLD);}
        int clue=cjClue(&cjkGame,i);
        if(clue>=0){char digit[2]={(char)('0'+clue),0};rect(a,b,9,10,BG);ascii(a,b,digit,GOLD);}
        // Number the selected path so a hint can be followed in order.
        if(order){char digit[2]={(char)('0'+order),0};rect(a+18,b+13,8,10,BG);ascii(a+18,b+13,digit,WHITE);}
    }
    sniprintf(msg,sizeof msg,"%lu",(unsigned long)cjkGame.score);nativeText(182,4,UI_SCORE,TEAL);ascii(182,23,msg,GOLD);
    button(37,UI_ACCEPT,cjSelected(&cjkGame)>=0?GOLD:WHITE);
    button(65,UI_CLEAR,WHITE);button(93,noHint?UI_RECOVER:UI_HINT,noHint||(lesson==1&&!cjkGame.len)?GOLD:TEAL);
    button(121,peek?UI_CANCEL:UI_OPEN,peek||lesson==2?GOLD:WHITE);
    button(149,dailyMode?UI_MENU:cjkGame.loss?UI_RETRY:cjComplete(&cjkGame)?UI_NEXT:UI_MENU,WHITE);
    if(lesson){
        ascii(5,178,"TRAINING",TEAL);
        sniprintf(msg,sizeof msg,"%d/4",lesson);ascii(216,178,msg,GOLD);
        box(78,178,130,8,PANEL,TEAL);rect(79,179,32*lesson,6,GOLD);
    }else ascii(5,178,"B: MENU   R: HINT   L: OPEN",TEAL);
}

void cjkLeave(void){
    finishRecord(true);
    recordPath=NULL;dailyDate=0;dailyMode=false;cjkDailyRanked=0;recorded=false;
    free(words);free(cjkPack);words=NULL;cjkPack=NULL;glyphs=NULL;uiLabels=NULL;ascii8=NULL;
    memset(&cjkGame,0,sizeof cjkGame);memset(&lex,0,sizeof lex);lang=-1;armed=-1;drag=suppress=false;
}
bool cjkEnter(int locale,u32 initialSeed,int date,bool training,int difficulty,const char *cfg){
    cjkLeave();
    if(!cjkAvailable(locale))return false;
    FILE *f=fopen(packPaths[locale],"rb");
    if(!f)return false;
    if(fseek(f,0,SEEK_END)){fclose(f);return false;}
    long bytes=ftell(f);
    if(bytes<16||bytes>65536||fseek(f,0,SEEK_SET)){fclose(f);return false;}
    cjkPack=malloc((size_t)bytes);
    if(!cjkPack){fclose(f);return false;}
    bool loaded=fread(cjkPack,1,(size_t)bytes,f)==(size_t)bytes;fclose(f);
    if(!loaded||memcmp(cjkPack,"CJK1",4))goto bad;
    unsigned count=rd16(cjkPack+4),ng=rd16(cjkPack+6),nu=rd16(cjkPack+8);
    if(!count||count>1024||!ng||ng>1024||nu!=UI_COUNT||rd16(cjkPack+10)||
       16u+76u*count+34u*ng+30u*nu+950u!=(unsigned)bytes||
       hash(cjkPack+16,(size_t)bytes-16)!=rd32(cjkPack+12))goto bad;
    words=calloc(count,sizeof *words);if(!words)goto bad;
    for(unsigned i=0;i<count;i++){
        const unsigned char *r=cjkPack+16+76*i;
        unsigned n=r[12];if(n<2||n>CJ_MAX||r[13]||!memchr(r+14,0,31)||!memchr(r+45,0,31))goto bad;
        for(unsigned j=0;j<n;j++){
            unsigned cp=rd16(r+2*j);if(cp<0x3000||(cp>=0xd800&&cp<=0xdfff)||(locale>=3&&(cp<0xe000||cp>=0xe400)))goto bad;
            words[i].text[j]=(uint16_t)cp;
        }
        words[i].len=(uint8_t)n;words[i].reading=(const char *)r+14;words[i].meaning=(const char *)r+45;
    }
    glyphs=(const CjGlyph *)(cjkPack+16+76*count);glyphCount=ng;
    for(unsigned i=1;i<ng;i++)if(glyphs[i-1].code>=glyphs[i].code)goto bad;
    uiLabels=(const uint16_t (*)[15])(cjkPack+16+76*count+34*ng);
    for(unsigned i=0;i<nu;i++)if(!uiLabels[i][0]||uiLabels[i][14])goto bad;
    // Check every referenced tile ID against the sorted atlas before play.
    for(unsigned i=0;i<count+nu;i++){
        const uint16_t *codes=i<count?words[i].text:uiLabels[i-count];
        unsigned n=i<count?words[i].len:14;
        for(unsigned j=0;j<n&&codes[j];j++){
            unsigned lo=0,hi=ng;
            while(lo<hi){unsigned m=(lo+hi)/2;if(glyphs[m].code<codes[j])lo=m+1;else hi=m;}
            if(lo==ng||glyphs[lo].code!=codes[j])goto bad;
        }
    }
    ascii8=(const uint8_t (*)[10])(cjkPack+16+76*count+34*ng+30*nu);
    lex.words=words;lex.count=(uint16_t)(locale<3?cjDailyVocabulary(count,date):count);
    bool daily=date!=0;
    if(!cjStartMode(&cjkGame,&lex,initialSeed,training?CJ_FIRE_OFF:daily?CJ_FIRE_NORMAL:difficulty,1,0))goto bad;
    lang=locale;dailyMode=daily;dailyDate=date;recordPath=cfg;recorded=false;
    // A date is consumed only AFTER resources and board generation succeed.
    // A missing or unwritable card leaves the same puzzle in practice mode.
    cjkDailyRanked=daily&&!training&&scriptDailyCareerClaim(cfg,recordLanguage(),date);
    resetInput();cjkEpoch++;return true;
bad:
    cjkLeave();return false;
}
bool cjkInput(u32 down,u32 held,u32 up,const touchPosition *t){
    if(!cjkPack)return false;
    dirty=false;
    if(down&(KEY_B|KEY_START))return false;
    if(cjEnded(&cjkGame)){
        if((down&KEY_A)||((down&KEY_TOUCH)&&t->px>=178&&t->px<252&&t->py>=149&&t->py<173))
            return nextOrRetry();
        return true;
    }
    if(down&(KEY_A|KEY_Y|KEY_R)){armed=-1;drag=false;suppress=true;peek=false;peelBlocked=false;
        if(down&KEY_A){noHint=recoveryBlocked=false;cjSubmit(&cjkGame);}
        else if(down&KEY_Y){cjkGame.len=0;noHint=recoveryBlocked=false;}
        else hint();
        dirty=true;
    }
    if((down&KEY_TOUCH)&&!(down&(KEY_A|KEY_B|KEY_START|KEY_Y|KEY_R))){
        suppress=false;drag=false;armed=-1;
        bool hintTap=t->px>=178&&t->px<252&&t->py>=93&&t->py<117;
        if(!hintTap){dirty|=noHint||recoveryBlocked||peelBlocked;noHint=recoveryBlocked=peelBlocked=false;}
        if(t->px>=178&&t->px<252){
            if(t->py>=37&&t->py<61)cjSubmit(&cjkGame);
            else if(t->py>=65&&t->py<89){cjkGame.len=0;peek=false;}
            else if(t->py>=93&&t->py<117)hint();
            else if(t->py>=121&&t->py<145){peek=!peek;cjkGame.len=0;}
            else if(t->py>=149&&t->py<173)return false;
            suppress=true;dirty=true;
        }else{
            int i=hit(t->px,t->py);
            if(i>=0){
                int x,y;origin(i,&x,&y);
                if(peek||(held&KEY_L))armed=i;
                else if((cjkGame.opened[i]&&!cjkGame.back[i])||(cjClue(&cjkGame,i)>=0&&!cjkGame.len&&t->px<x+9&&t->py<y+10)){
                    cjkGame.focus=(int8_t)i;dirty=true;suppress=true;
                }else{drag=true;dirty|=cjAdd(&cjkGame,i);}
            }
        }
    }
    if((held&KEY_TOUCH)&&!suppress){
        int i=hit(t->px,t->py);
        if(armed>=0&&i!=armed)armed=-1;
        else if(drag&&i>=0)dirty|=cjAdd(&cjkGame,i);
    }
    if(up&KEY_TOUCH){
        if(armed>=0){peelBlocked=cjPeel(&cjkGame,armed)==-2;peek=false;dirty=true;}
        armed=-1;drag=suppress=false;
    }
    if(dirty){finishRecord(false);cjkEpoch++;}
    return true;
}
