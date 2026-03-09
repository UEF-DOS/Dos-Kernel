#include <stdint.h>

#define SYS_EXIT      0
#define SYS_FB_WIDTH  2
#define SYS_FB_HEIGHT 3
#define SYS_FB_PITCH  4
#define SYS_MAP_FB    5
#define SYS_GETKEY    6
#define SYS_POLL_KEY  7
#define SYS_SLEEP_MS  8

static inline uint64_t sc0(uint64_t n) {
    uint64_t r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n) : "memory");
    return r;
}
static inline uint64_t sc1(uint64_t n, uint64_t a) {
    uint64_t r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n),"D"(a) : "memory");
    return r;
}

/* ── fixed-point 16.16 ── */
typedef int32_t fix;
#define FIX(x)      ((fix)((x) * 65536.0f))
#define FIXI(x)     ((fix)((x) << 16))
#define FMUL(a,b)   ((fix)(((int64_t)(a)*(b)) >> 16))
#define FINT(a)     ((a) >> 16)

/* ── trig LUT (512 steps = 2π) ── */
#define TS 512
static fix SIN[TS], COS[TS];
static void build_trig(void) {
    for (int i = 0; i < TS; i++) {
        int hq = TS/4, r = i % hq;
        int sv = (4*r*(hq-r)*65536)/(hq*hq);
        int sqi = i/hq;
        SIN[i] = (sqi==0||sqi==1) ? sv : -sv;
        int ci = (i+TS/4)%TS, cr = ci%hq;
        int cv = (4*cr*(hq-cr)*65536)/(hq*hq);
        int cqi = ci/hq;
        COS[i] = (cqi==0||cqi==1) ? cv : -cv;
    }
}
static fix fsin(int a){ return SIN[((a%TS)+TS)%TS]; }
static fix fcos(int a){ return COS[((a%TS)+TS)%TS]; }

/* ── RNG ── */
static uint32_t rseed = 0xBEEFCAFE;
static uint32_t rnd(void){
    rseed ^= rseed<<13; rseed ^= rseed>>17; rseed ^= rseed<<5;
    return rseed;
}
static int rnd_range(int lo, int hi){ return lo + (int)(rnd() % (uint32_t)(hi-lo)); }

/* ── framebuffer ── */
static uint32_t *fb;
static int W, H, PITCH;

static void pset(int x, int y, uint32_t c){
    x = ((x%W)+W)%W; y = ((y%H)+H)%H;
    fb[y*PITCH+x] = c;
}
static void fill(uint32_t c){
    for(int i=0;i<H*PITCH;i++) fb[i]=c;
}

/* draw a line with wrap */
static void line(int x0,int y0,int x1,int y1,uint32_t c){
    int dx=x1-x0, dy=y1-y0;
    int ax=dx<0?-dx:dx, ay=dy<0?-dy:dy;
    int sx=dx<0?-1:1, sy=dy<0?-1:1;
    int err=ax-ay;
    for(;;){
        pset(x0,y0,c);
        if(x0==x1&&y0==y1) break;
        int e2=err*2;
        if(e2>-ay){err-=ay;x0+=sx;}
        if(e2< ax){err+=ax;y0+=sy;}
    }
}

/* draw a filled circle */
static void circle_fill(int cx,int cy,int r,uint32_t c){
    for(int y=-r;y<=r;y++)
        for(int x=-r;x<=r;x++)
            if(x*x+y*y<=r*r) pset(cx+x,cy+y,c);
}

/* draw an outlined circle (Bresenham) */
static void circle_outline(int cx,int cy,int r,uint32_t c){
    int x=0,y=r,d=3-2*r;
    while(y>=x){
        pset(cx+x,cy+y,c); pset(cx-x,cy+y,c);
        pset(cx+x,cy-y,c); pset(cx-x,cy-y,c);
        pset(cx+y,cy+x,c); pset(cx-y,cy+x,c);
        pset(cx+y,cy-x,c); pset(cx-y,cy-x,c);
        if(d<0) d+=4*x+6; else{d+=4*(x-y)+10;y--;}
        x++;
    }
}

/* ── 5x7 font (uppercase + digits) ── */
static const uint8_t FONT[36][7] = {
    /* 0-9 */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /* A-Z */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x01,0x01,0x01,0x01,0x01,0x11,0x0E},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x15,0x15,0x15,0x15,0x0A},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

static int char_idx(char c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='A'&&c<='Z') return 10+c-'A';
    if(c>='a'&&c<='z') return 10+c-'a';
    return -1;
}

static void draw_char(int px,int py,char c,uint32_t col,int scale){
    int idx=char_idx(c);
    if(idx<0) return;
    for(int row=0;row<7;row++){
        uint8_t bits=FONT[idx][row];
        for(int bit=4;bit>=0;bit--){
            if(bits&(1<<bit)){
                int bx=px+(4-bit)*scale;
                int by=py+row*scale;
                for(int sy=0;sy<scale;sy++)
                    for(int sx=0;sx<scale;sx++)
                        pset(bx+sx,by+sy,col);
            }
        }
    }
}

static void draw_str(int px,int py,const char*s,uint32_t col,int scale){
    int x=px;
    while(*s){
        if(*s==' '){x+=6*scale;s++;continue;}
        draw_char(x,py,*s,col,scale);
        x+=6*scale; s++;
    }
}

static void draw_int(int px,int py,int n,uint32_t col,int scale){
    if(n<0){draw_char(px,py,'-',col,scale);px+=6*scale;n=-n;}
    char buf[12]; int len=0;
    if(n==0){draw_char(px,py,'0',col,scale);return;}
    while(n>0){buf[len++]='0'+n%10;n/=10;}
    for(int i=len-1;i>=0;i--){draw_char(px,py,buf[i],col,scale);px+=6*scale;}
}

/* ── vector ship rendering ── */
/* ship points (local space, scale ×8 for clarity) */
static void draw_ship(int cx,int cy,int angle,uint32_t col,int thrusting){
    /* nose, left wing, right wing, tail-left, tail-right */
    fix ns = fsin(angle), nc = fcos(angle);
    fix ls = fsin(angle+TS*2/3), lc = fcos(angle+TS*2/3);
    fix rs = fsin(angle-TS*2/3), rc = fcos(angle-TS*2/3);

    int nx=cx+FINT(nc*12), ny=cy+FINT(ns*12);
    int lx=cx+FINT(lc*10), ly=cy+FINT(ls*10);
    int rx=cx+FINT(rc*10), ry=cy+FINT(rs*10);
    /* inner tail points */
    int tlx=cx+FINT(fcos(angle+TS*2/3)*4), tly=cy+FINT(fsin(angle+TS*2/3)*4);
    int trx=cx+FINT(fcos(angle-TS*2/3)*4), try_=cy+FINT(fsin(angle-TS*2/3)*4);

    line(nx,ny,lx,ly,col);
    line(nx,ny,rx,ry,col);
    line(lx,ly,tlx,tly,col);
    line(rx,ry,trx,try_,col);
    line(tlx,tly,trx,try_,col);

    if(thrusting && (rseed>>24)&1){
        /* flame flicker */
        int fa = (angle+TS/2)%TS;
        int fl = rnd_range(6,14);
        int fx=cx+FINT(fcos(fa)*fl), fy=cy+FINT(fsin(fa)*fl);
        int fla=(fa+rnd_range(-20,20)+TS)%TS;
        int flb=(fa+rnd_range(-20,20)+TS)%TS;
        int fx2=cx+FINT(fcos(fla)*5), fy2=cy+FINT(fsin(fla)*5);
        int fx3=cx+FINT(fcos(flb)*5), fy3=cy+FINT(fsin(flb)*5);
        line(fx2,fy2,fx,fy,0xFF6600);
        line(fx3,fy3,fx,fy,0xFF3300);
    }
}

/* draw a jagged asteroid polygon */
static void draw_asteroid(int cx,int cy,int r,int seed,uint32_t col){
    int steps=10;
    int pts_x[10], pts_y[10];
    for(int i=0;i<steps;i++){
        int a=i*TS/steps;
        /* jag radius using seed */
        uint32_t h=seed^(i*2654435761u);
        h^=h>>16; h*=0x45d9f3b; h^=h>>16;
        int jr=r - r/3 + (int)(h%(uint32_t)(r*2/3));
        pts_x[i]=cx+FINT(fcos(a)*jr);
        pts_y[i]=cy+FINT(fsin(a)*jr);
    }
    for(int i=0;i<steps;i++){
        int j=(i+1)%steps;
        line(pts_x[i],pts_y[i],pts_x[j],pts_y[j],col);
    }
}

/* ── game state ── */
#define MAX_BULLETS   20
#define MAX_ASTEROIDS 24
#define MAX_PARTICLES 128
#define LIVES_START   3

typedef struct { fix x,y,vx,vy; int life; uint32_t col; } Particle;
typedef struct { fix x,y,vx,vy; int r; int alive; int seed; } Asteroid;
typedef struct { fix x,y,vx,vy; int life; } Bullet;

static fix     sx,sy,svx,svy;   /* ship position/velocity */
static int     sangle;           /* ship angle */
static int     thrusting;
static int     lives, score, level_num;
static int     invincible;       /* frames of invincibility after respawn */
static int     fire_cd;

static Bullet    bullets[MAX_BULLETS];
static Asteroid  rocks[MAX_ASTEROIDS];
static Particle  parts[MAX_PARTICLES];
static int       rock_count;

/* spawn a particle burst */
static void burst(fix x,fix y,int n,uint32_t col,int speed){
    for(int i=0;i<n;i++){
        for(int j=0;j<MAX_PARTICLES;j++){
            if(parts[j].life>0) continue;
            int a=rnd()%TS;
            int spd=rnd_range(speed/2,speed);
            parts[j].x=x; parts[j].y=y;
            parts[j].vx=FMUL(fcos(a),FIXI(spd));
            parts[j].vy=FMUL(fsin(a),FIXI(spd));
            parts[j].life=rnd_range(15,40);
            parts[j].col=col;
            break;
        }
    }
}

/* split or remove asteroid */
static void split_rock(int i,fix bvx,fix bvy){
    int r=rocks[i].r;
    int cx=FINT(rocks[i].x), cy=FINT(rocks[i].y);

    burst(rocks[i].x,rocks[i].y,12,0xFF8800,2);

    if(r>18){
        score += 20;
        /* split into 2 smaller */
        int nr=r/2;
        for(int k=0;k<2;k++){
            for(int j=0;j<MAX_ASTEROIDS;j++){
                if(rocks[j].alive) continue;
                int a=rnd()%TS;
                fix spd=FIX(0.8f)+rnd()%FIX(1.0f);
                rocks[j].x=rocks[i].x+FIXI(rnd_range(-8,8));
                rocks[j].y=rocks[i].y+FIXI(rnd_range(-8,8));
                rocks[j].vx=FMUL(fcos(a),spd);
                rocks[j].vy=FMUL(fsin(a),spd);
                rocks[j].r=nr;
                rocks[j].alive=1;
                rocks[j].seed=rnd();
                break;
            }
        }
        rocks[i].alive=0;
    } else if(r>10){
        score += 50;
        /* split into 2 tiny */
        for(int k=0;k<2;k++){
            for(int j=0;j<MAX_ASTEROIDS;j++){
                if(rocks[j].alive) continue;
                int a=rnd()%TS;
                fix spd=FIX(1.2f)+rnd()%FIX(1.5f);
                rocks[j].x=rocks[i].x+FIXI(rnd_range(-4,4));
                rocks[j].y=rocks[i].y+FIXI(rnd_range(-4,4));
                rocks[j].vx=FMUL(fcos(a),spd);
                rocks[j].vy=FMUL(fsin(a),spd);
                rocks[j].r=6;
                rocks[j].alive=1;
                rocks[j].seed=rnd();
                break;
            }
        }
        rocks[i].alive=0;
    } else {
        score += 100;
        rocks[i].alive=0;
    }
    (void)bvx;(void)bvy;(void)cx;(void)cy;
}

static int rocks_alive(void){
    int n=0;
    for(int i=0;i<MAX_ASTEROIDS;i++) if(rocks[i].alive) n++;
    return n;
}

static void spawn_wave(void){
    int n=3+level_num*2;
    if(n>MAX_ASTEROIDS) n=MAX_ASTEROIDS;
    for(int i=0;i<MAX_ASTEROIDS;i++) rocks[i].alive=0;
    for(int k=0;k<n;k++){
        /* spawn off-center from ship */
        int a=rnd()%TS;
        int dist=rnd_range(W/4,W/2);
        fix rx=sx+FIXI(FINT(fcos(a)*dist));
        fix ry=sy+FIXI(FINT(fsin(a)*dist));
        int va=rnd()%TS;
        fix spd=FIX(0.4f)+rnd()%FIX(0.8f);
        rocks[k].x=rx; rocks[k].y=ry;
        rocks[k].vx=FMUL(fcos(va),spd);
        rocks[k].vy=FMUL(fsin(va),spd);
        rocks[k].r=rnd_range(22,32);
        rocks[k].alive=1;
        rocks[k].seed=rnd();
    }
}

static void respawn_ship(void){
    sx=FIXI(W/2); sy=FIXI(H/2);
    svx=0; svy=0;
    sangle=TS*3/4; /* pointing up */
    invincible=120;
    for(int i=0;i<MAX_BULLETS;i++) bullets[i].life=0;
}

static void init_game(void){
    lives=LIVES_START;
    score=0;
    level_num=1;
    fire_cd=0;
    for(int i=0;i<MAX_PARTICLES;i++) parts[i].life=0;
    for(int i=0;i<MAX_BULLETS;i++)   bullets[i].life=0;
    respawn_ship();
    spawn_wave();
}

/* ── draw starfield (deterministic from index) ── */
static void draw_stars(void){
    for(int i=0;i<80;i++){
        uint32_t h=i*2654435761u; h^=h>>16; h*=0x45d9f3b; h^=h>>16;
        int x=(int)(h%W);
        uint32_t h2=(i+1234)*2654435761u; h2^=h2>>16; h2*=0x45d9f3b; h2^=h2>>16;
        int y=(int)(h2%H);
        uint8_t br=40+((h>>8)&63);
        pset(x,y,((uint32_t)br<<16)|((uint32_t)br<<8)|br);
    }
}

/* ── HUD ── */
static void draw_hud(void){
    /* score */
    draw_str(8,8,"SCORE",0x888888,1);
    draw_int(8,18,score,0xFFFFFF,1);
    /* lives */
    for(int i=0;i<lives;i++){
        int bx=W-20-i*16, by=8;
        /* tiny ship icon */
        line(bx,by-5,bx-4,by+4,0x00FF88);
        line(bx,by-5,bx+4,by+4,0x00FF88);
        line(bx-3,by+3,bx+3,by+3,0x00FF88);
    }
    /* level */
    draw_str(W/2-12,8,"LV",0x555555,1);
    draw_int(W/2+2,8,level_num,0x888888,1);
}

/* ── main ── */
void _start(void){
    build_trig();
    W     =(int)sc0(SYS_FB_WIDTH);
    H     =(int)sc0(SYS_FB_HEIGHT);
    PITCH =(int)(sc0(SYS_FB_PITCH)/4);
    fb    =(uint32_t*)sc0(SYS_MAP_FB);

    init_game();

    /* key state — track held keys via repeated poll */
    int k_left=0,k_right=0,k_thrust=0,k_fire=0;

restart_outer:
    init_game();

    for(;;){
        /* ── input ── */
        uint8_t key=(uint8_t)sc0(SYS_POLL_KEY);
        if(key=='a'||key=='A') k_left=3;
        if(key=='d'||key=='D') k_right=3;
        if(key=='w'||key=='W') k_thrust=3;
        if(key==' ')           k_fire=1;
        if(key=='q'||key=='Q') sc0(SYS_EXIT);
        if(k_left>0)  k_left--;
        if(k_right>0) k_right--;
        if(k_thrust>0)k_thrust--;

        thrusting=0;

        /* ── update ship ── */
        if(k_left)  sangle=(sangle-8+TS)%TS;
        if(k_right) sangle=(sangle+8)%TS;
        if(k_thrust){
            svx+=FMUL(fcos(sangle),FIX(0.18f));
            svy+=FMUL(fsin(sangle),FIX(0.18f));
            thrusting=1;
        }
        /* drag */
        svx=FMUL(svx,FIX(0.985f));
        svy=FMUL(svy,FIX(0.985f));
        /* clamp speed */
        fix spd2=FMUL(svx,svx)+FMUL(svy,svy);
        if(spd2>FMUL(FIX(5.0f),FIX(5.0f))){
            svx=FMUL(svx,FIX(0.95f));
            svy=FMUL(svy,FIX(0.95f));
        }
        sx+=svx; sy+=svy;
        /* wrap */
        if(sx<0) sx+=FIXI(W); if(sx>=FIXI(W)) sx-=FIXI(W);
        if(sy<0) sy+=FIXI(H); if(sy>=FIXI(H)) sy-=FIXI(H);

        /* ── fire ── */
        fire_cd--;
        if(k_fire && fire_cd<=0){
            k_fire=0; fire_cd=10;
            for(int i=0;i<MAX_BULLETS;i++){
                if(bullets[i].life>0) continue;
                fix bspd=FIX(5.5f);
                bullets[i].x=sx+FMUL(fcos(sangle),FIXI(13));
                bullets[i].y=sy+FMUL(fsin(sangle),FIXI(13));
                bullets[i].vx=svx+FMUL(fcos(sangle),bspd);
                bullets[i].vy=svy+FMUL(fsin(sangle),bspd);
                bullets[i].life=55;
                break;
            }
        }

        /* ── update bullets ── */
        for(int i=0;i<MAX_BULLETS;i++){
            if(!bullets[i].life) continue;
            bullets[i].x+=bullets[i].vx;
            bullets[i].y+=bullets[i].vy;
            if(bullets[i].x<0) bullets[i].x+=FIXI(W);
            if(bullets[i].x>=FIXI(W)) bullets[i].x-=FIXI(W);
            if(bullets[i].y<0) bullets[i].y+=FIXI(H);
            if(bullets[i].y>=FIXI(H)) bullets[i].y-=FIXI(H);
            bullets[i].life--;
        }

        /* ── update asteroids ── */
        for(int i=0;i<MAX_ASTEROIDS;i++){
            if(!rocks[i].alive) continue;
            rocks[i].x+=rocks[i].vx;
            rocks[i].y+=rocks[i].vy;
            if(rocks[i].x<0) rocks[i].x+=FIXI(W);
            if(rocks[i].x>=FIXI(W)) rocks[i].x-=FIXI(W);
            if(rocks[i].y<0) rocks[i].y+=FIXI(H);
            if(rocks[i].y>=FIXI(H)) rocks[i].y-=FIXI(H);
        }

        /* ── bullet ↔ asteroid collision ── */
        for(int b=0;b<MAX_BULLETS;b++){
            if(!bullets[b].life) continue;
            int bx=FINT(bullets[b].x), by=FINT(bullets[b].y);
            for(int r=0;r<MAX_ASTEROIDS;r++){
                if(!rocks[r].alive) continue;
                int rx=FINT(rocks[r].x), ry=FINT(rocks[r].y);
                int dx=bx-rx, dy=by-ry;
                if(dx*dx+dy*dy < rocks[r].r*rocks[r].r){
                    split_rock(r,bullets[b].vx,bullets[b].vy);
                    bullets[b].life=0;
                    break;
                }
            }
        }

        /* ── ship ↔ asteroid collision ── */
        if(invincible>0){
            invincible--;
        } else {
            int shipx=FINT(sx), shipy=FINT(sy);
            for(int r=0;r<MAX_ASTEROIDS;r++){
                if(!rocks[r].alive) continue;
                int rx=FINT(rocks[r].x), ry=FINT(rocks[r].y);
                int dx=shipx-rx, dy=shipy-ry;
                int hit_r=rocks[r].r-2;
                if(dx*dx+dy*dy < hit_r*hit_r){
                    burst(sx,sy,20,0x00FF88,3);
                    lives--;
                    if(lives<=0){
                        /* game over */
                        fill(0x000000);
                        draw_stars();
                        draw_str(W/2-36,H/2-20,"GAME OVER",0xFF3333,2);
                        draw_str(W/2-42,H/2+10,"SCORE",0x888888,1);
                        draw_int(W/2+6,H/2+10,score,0xFFFFFF,1);
                        draw_str(W/2-42,H/2+24,"R RESTART",0x555555,1);
                        for(;;){
                            uint8_t k=(uint8_t)sc0(SYS_GETKEY);
                            if(k=='r'||k=='R') goto restart_outer;
                            if(k=='q'||k=='Q') sc0(SYS_EXIT);
                        }
                    }
                    respawn_ship();
                    break;
                }
            }
        }

        /* ── update particles ── */
        for(int i=0;i<MAX_PARTICLES;i++){
            if(!parts[i].life) continue;
            parts[i].x+=parts[i].vx;
            parts[i].y+=parts[i].vy;
            parts[i].life--;
        }

        /* ── next wave ── */
        if(rocks_alive()==0){
            level_num++;
            burst(sx,sy,30,0xFFFF00,2);
            spawn_wave();
        }

        /* ── draw ── */
        fill(0x000000);
        draw_stars();

        /* particles */
        for(int i=0;i<MAX_PARTICLES;i++){
            if(!parts[i].life) continue;
            int fade=parts[i].life;
            uint32_t c=parts[i].col;
            uint8_t r2=((c>>16)&0xFF)*fade/40;
            uint8_t g2=((c>>8 )&0xFF)*fade/40;
            uint8_t b2=((c    )&0xFF)*fade/40;
            pset(FINT(parts[i].x),FINT(parts[i].y),
                 ((uint32_t)r2<<16)|((uint32_t)g2<<8)|b2);
        }

        /* asteroids */
        for(int i=0;i<MAX_ASTEROIDS;i++){
            if(!rocks[i].alive) continue;
            draw_asteroid(FINT(rocks[i].x),FINT(rocks[i].y),
                          rocks[i].r,rocks[i].seed,0xAAAAAA);
            /* inner detail ring */
            draw_asteroid(FINT(rocks[i].x),FINT(rocks[i].y),
                          rocks[i].r*2/3,rocks[i].seed^0xDEAD,0x555555);
        }

        /* bullets */
        for(int i=0;i<MAX_BULLETS;i++){
            if(!bullets[i].life) continue;
            int bx=FINT(bullets[i].x), by=FINT(bullets[i].y);
            pset(bx,by,0xFFFFFF);
            pset(bx+1,by,0xFFFF88);
            pset(bx-1,by,0xFFFF88);
            pset(bx,by+1,0xFFFF88);
            pset(bx,by-1,0xFFFF88);
        }

        /* ship (blink when invincible) */
        if(invincible==0 || (invincible/4)&1)
            draw_ship(FINT(sx),FINT(sy),sangle,0x00FF88,thrusting);

        draw_hud();

        sc1(SYS_SLEEP_MS,16);
    }
}