#include <stdint.h>

#define SYS_EXIT      0
#define SYS_FB_WIDTH  2
#define SYS_FB_HEIGHT 3
#define SYS_FB_PITCH  4
#define SYS_MAP_FB    5
#define SYS_POLL_KEY  7
#define SYS_SLEEP_MS  8

static inline uint64_t sc0(uint64_t n){
    uint64_t r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n):"memory");
    return r;
}
static inline uint64_t sc1(uint64_t n,uint64_t a){
    uint64_t r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a):"memory");
    return r;
}

static uint32_t *fb;
static int FBW, FBH, PITCH;

static void pset(int x,int y,uint32_t c){
    if((unsigned)x>=(unsigned)FBW||(unsigned)y>=(unsigned)FBH) return;
    fb[y*PITCH+x]=c;
}
static void fill_rect(int x,int y,int w,int h,uint32_t c){
    for(int r=y;r<y+h;r++) for(int col=x;col<x+w;col++) pset(col,r,c);
}
static void outline_rect(int x,int y,int w,int h,uint32_t c){
    for(int i=x;i<x+w;i++){ pset(i,y,c); pset(i,y+h-1,c); }
    for(int i=y;i<y+h;i++){ pset(x,i,c); pset(x+w-1,i,c); }
}
static void clear_screen(void){
    for(int i=0;i<FBW*FBH;i++) fb[i]=0x000000;
}

/* ── tiny font 5x7 ───────────────────────────────────────────────── */
static const uint8_t FONT[36][7]={
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    {0x11,0x11,0x15,0x15,0x15,0x0A,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};
static int char_idx(char c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='A'&&c<='Z') return 10+(c-'A');
    if(c>='a'&&c<='z') return 10+(c-'a');
    return -1;
}
static void draw_str(int px,int py,const char*s,uint32_t col,int scale){
    while(*s){
        int idx=char_idx(*s);
        if(idx>=0)
            for(int row=0;row<7;row++){
                uint8_t bits=FONT[idx][row];
                for(int bit=4;bit>=0;bit--)
                    if(bits&(1<<bit))
                        fill_rect(px+(4-bit)*scale,py+row*scale,scale,scale,col);
            }
        px+=(5+1)*scale; s++;
    }
}
static void itoa_u(unsigned v,char*buf){
    if(v==0){buf[0]='0';buf[1]=0;return;}
    char tmp[12];int n=0;
    while(v>0){tmp[n++]='0'+v%10;v/=10;}
    int i=0;for(int j=n-1;j>=0;j--) buf[i++]=tmp[j];
    buf[i]=0;
}

/* ── game ────────────────────────────────────────────────────────── */
#define COLS 10
#define ROWS  6
#define BGAP  3
#define PAD_H 12
#define BALL_R 6

static int bricks[ROWS][COLS];
static int bricks_left;
static int brick_w, brick_h, bricks_ox, bricks_oy;
static int pad_x, pad_y, pad_w;
static int ball_x, ball_y;    /* *256 */
static int ball_vx, ball_vy;  /* *256/frame */
static int score, lives, level;
static int state; /* 1=play 2=dead 3=win */

static const uint32_t ROW_COL[ROWS]={
    0xFF2222,0xFF8800,0xFFDD00,0x22CC22,0x2288FF,0xBB44FF
};

static int absi(int v){ return v<0?-v:v; }

static void layout(void){
    brick_w  = (FBW-40)/COLS - BGAP;
    brick_h  = 16;
    bricks_ox= (FBW-(COLS*(brick_w+BGAP)-BGAP))/2;
    bricks_oy= 50;
    pad_y    = FBH-50;
}

static void init_level(void){
    bricks_left=0;
    for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++){
        int hp=(level>=3&&r<2)?3:(level>=2&&r<3)?2:1;
        bricks[r][c]=hp; bricks_left++;
    }
    pad_w=FBW/5-(level-1)*10; if(pad_w<60) pad_w=60;
    pad_x=FBW/2-pad_w/2;
    ball_x=(FBW/2)<<8; ball_y=(pad_y-BALL_R-4)<<8;
    int spd=(3+level)<<8;
    ball_vx=spd; ball_vy=-spd;
}

static void game_init(void){
    score=0;lives=3;level=1;state=1;
    layout(); init_level();
}

static void brick_pos(int r,int c,int*bx,int*by){
    *bx=bricks_ox+c*(brick_w+BGAP);
    *by=bricks_oy+r*(brick_h+BGAP);
}

static void draw_bricks(void){
    for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++){
        if(!bricks[r][c]) continue;
        int bx,by; brick_pos(r,c,&bx,&by);
        uint32_t col=ROW_COL[r];
        if(bricks[r][c]==2){
            uint8_t rr=(col>>16)&0xFF,gg=(col>>8)&0xFF,bb=col&0xFF;
            col=((uint32_t)(rr*2/3)<<16)|((uint32_t)(gg*2/3)<<8)|(bb*2/3);
        } else if(bricks[r][c]>=3){
            uint8_t rr=(col>>16)&0xFF,gg=(col>>8)&0xFF,bb=col&0xFF;
            col=((uint32_t)(rr/2)<<16)|((uint32_t)(gg/2)<<8)|(bb/2);
        }
        fill_rect(bx+1,by+1,brick_w-2,brick_h-2,col);
        /* top shine */
        uint32_t hi=(uint32_t)(((col>>16)&0xFF)+80>255?255:((col>>16)&0xFF)+80)<<16
                   |(uint32_t)(((col>>8)&0xFF)+80>255?255:((col>>8)&0xFF)+80)<<8
                   |(uint32_t)((col&0xFF)+80>255?255:(col&0xFF)+80);
        for(int i=bx+2;i<bx+brick_w-2;i++) pset(i,by+2,hi);
        outline_rect(bx,by,brick_w,brick_h,0x000000);
    }
}

static void draw_paddle(void){
    fill_rect(pad_x,pad_y,pad_w,PAD_H,0x4488BB);
    fill_rect(pad_x+2,pad_y+1,pad_w-4,3,0x99CCEE);
    fill_rect(pad_x+2,pad_y+PAD_H-3,pad_w-4,2,0x223344);
    outline_rect(pad_x,pad_y,pad_w,PAD_H,0x88CCFF);
}

static void draw_ball(void){
    int bx=ball_x>>8, by=ball_y>>8;
    for(int dy=-BALL_R;dy<=BALL_R;dy++)
        for(int dx=-BALL_R;dx<=BALL_R;dx++)
            if(dx*dx+dy*dy<=BALL_R*BALL_R)
                pset(bx+dx,by+dy,0xDDEEFF);
    pset(bx-1,by-2,0xFFFFFF);
    pset(bx-2,by-1,0xFFFFFF);
}

static void draw_hud(void){
    char buf[16];
    draw_str(8,8,"SCORE",0x446644,1);
    itoa_u((unsigned)score,buf);
    draw_str(8,17,buf,0xFFFFFF,2);
    draw_str(FBW-42,8,"LV",0x446644,1);
    itoa_u((unsigned)level,buf);
    draw_str(FBW-30,17,buf,0xFFDD00,2);
    /* lives as dots */
    draw_str(FBW/2-18,8,"LF",0x446644,1);
    for(int i=0;i<lives;i++) fill_rect(FBW/2-6+i*10,17,7,7,0xFF4444);
    /* bottom bar */
    for(int x=0;x<FBW;x++) pset(x,FBH-18,0x1a1a1a);
    draw_str(4,FBH-13,"A D  MOVE    X  QUIT    N  RESTART",0x333333,1);
}

static void update(int ka,int kd){
    int pspd=8+level;
    if(ka){ pad_x-=pspd; if(pad_x<0) pad_x=0; }
    if(kd){ pad_x+=pspd; if(pad_x>FBW-pad_w) pad_x=FBW-pad_w; }

    ball_x+=ball_vx; ball_y+=ball_vy;
    int bx=ball_x>>8, by=ball_y>>8;

    /* walls */
    if(bx-BALL_R<0){    ball_x=BALL_R<<8;          ball_vx= absi(ball_vx); }
    if(bx+BALL_R>=FBW){ ball_x=(FBW-1-BALL_R)<<8;  ball_vx=-absi(ball_vx); }
    if(by-BALL_R<0){    ball_y=BALL_R<<8;           ball_vy= absi(ball_vy); }

    /* lost */
    if(by>FBH+40){
        lives--;
        if(lives<=0){ state=2; return; }
        ball_x=(FBW/2)<<8; ball_y=(pad_y-BALL_R-4)<<8;
        int spd=(3+level)<<8; ball_vx=spd; ball_vy=-spd;
        return;
    }

    /* paddle */
    if(ball_vy>0 &&
       by+BALL_R>=pad_y && by+BALL_R<=pad_y+PAD_H+4 &&
       bx>=pad_x-BALL_R && bx<=pad_x+pad_w+BALL_R){
        ball_vy=-absi(ball_vy);
        int rel=bx-(pad_x+pad_w/2);
        ball_vx=(rel<<8)/(pad_w/2);
        if(ball_vx==0) ball_vx=128;
        int maxvx=(4+level)<<8;
        if(ball_vx> maxvx) ball_vx= maxvx;
        if(ball_vx<-maxvx) ball_vx=-maxvx;
        ball_y=(pad_y-BALL_R)<<8;
    }

    /* bricks */
    bx=ball_x>>8; by=ball_y>>8;
    for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++){
        if(!bricks[r][c]) continue;
        int brx,bry; brick_pos(r,c,&brx,&bry);
        int nx=bx<brx?brx:(bx>brx+brick_w?brx+brick_w:bx);
        int ny=by<bry?bry:(by>bry+brick_h?bry+brick_h:by);
        int ddx=bx-nx, ddy=by-ny;
        if(ddx*ddx+ddy*ddy>BALL_R*BALL_R) continue;

        bricks[r][c]--;
        if(!bricks[r][c]){ bricks_left--; score+=10*(r+1)*level; }

        /* axis of least penetration */
        int ol=bx-(brx-BALL_R), or2=(brx+brick_w+BALL_R)-bx;
        int ot=by-(bry-BALL_R), ob=(bry+brick_h+BALL_R)-by;
        int minh=ol<or2?ol:or2, minv=ot<ob?ot:ob;
        if(minh<minv) ball_vx=bx<brx+brick_w/2?-absi(ball_vx):absi(ball_vx);
        else          ball_vy=by<bry+brick_h/2?-absi(ball_vy):absi(ball_vy);
        goto done;
    }
    done:;

    if(bricks_left==0){
        level++; if(level>5){ state=3; return; }
        init_level();
    }
}

void _start(void){
    FBW   =(int)sc0(SYS_FB_WIDTH);
    FBH   =(int)sc0(SYS_FB_HEIGHT);
    PITCH =(int)(sc0(SYS_FB_PITCH)/4);
    fb    =(uint32_t*)sc0(SYS_MAP_FB);

    game_init();
    int ka=0,kd=0;

    for(;;){
        uint8_t key=(uint8_t)sc0(SYS_POLL_KEY);
        if(key=='x'||key=='X') sc0(SYS_EXIT);
        if(key=='a'||key=='A') ka=5;
        if(key=='d'||key=='D') kd=5;
        if((key=='n'||key=='N')&&state!=1) game_init();

        clear_screen();

        if(state==1){
            update(ka,kd);
            draw_bricks();
            draw_paddle();
            draw_ball();
            draw_hud();
        } else if(state==2){
            draw_str(FBW/2-54,FBH/2-24,"GAME OVER",0xFF3300,2);
            char buf[16]; itoa_u((unsigned)score,buf);
            draw_str(FBW/2-24,FBH/2+4,buf,0xFFFFFF,2);
            draw_str(FBW/2-60,FBH/2+28,"PRESS N TO RETRY",0x555555,1);
        } else {
            draw_str(FBW/2-42,FBH/2-24,"YOU WIN",0xFFDD00,2);
            char buf[16]; itoa_u((unsigned)score,buf);
            draw_str(FBW/2-24,FBH/2+4,buf,0xFFFFFF,2);
            draw_str(FBW/2-54,FBH/2+28,"PRESS N TO PLAY",0x555555,1);
        }

        if(ka>0) ka--;
        if(kd>0) kd--;

        sc1(SYS_SLEEP_MS,16);
    }
}