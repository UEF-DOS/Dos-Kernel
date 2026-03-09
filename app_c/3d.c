#include <stdint.h>

#define SYS_EXIT      0
#define SYS_FB_WIDTH  2
#define SYS_FB_HEIGHT 3
#define SYS_FB_PITCH  4
#define SYS_MAP_FB    5
#define SYS_GETKEY    6
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

/* ── fixed-point 16.16 ───────────────────────────────────────────── */
typedef int32_t fix;
#define FIX(x)    ((fix)((x)*65536.0f))
#define FMUL(a,b) ((fix)(((int64_t)(a)*(b))>>16))
#define FDIV(a,b) ((fix)(((int64_t)(a)<<16)/(b)))
#define FINT(a)   ((a)>>16)
#define FIXI(x)   ((fix)((x)<<16))

/* ── trig LUT  (1024 steps = 2π) ────────────────────────────────── */
#define TS 1024
static fix SINL[TS], COSL[TS];

static void build_trig(void){
    int half = TS/2;
    for(int i=0;i<TS;i++){
        int h = i % half;
        int64_t sv = (int64_t)4*h*(half-h)*65536/((int64_t)half*half);
        SINL[i] = (i < half) ? (fix)sv : -(fix)sv;
        int ci = (i+TS/4)%TS;
        int ch = ci % half;
        int64_t cv = (int64_t)4*ch*(half-ch)*65536/((int64_t)half*half);
        COSL[i] = (ci < half) ? (fix)cv : -(fix)cv;
    }
}

static fix fsin(int a){ return SINL[((a%TS)+TS)%TS]; }
static fix fcos(int a){ return COSL[((a%TS)+TS)%TS]; }

/* ── RNG ─────────────────────────────────────────────────────────── */
static uint32_t rseed = 0xCAFEBABE;
static uint32_t rnd(void){
    rseed^=rseed<<13; rseed^=rseed>>17; rseed^=rseed<<5;
    return rseed;
}

/* ── framebuffer + back buffer ───────────────────────────────────── */
static uint32_t *fb;
static int FBW, FBH, FBPITCH;

/* render at half resolution into back buffer, then blit 2x2 */
#define MAX_RW 960
#define MAX_RH 540
static uint32_t backbuf[MAX_RW * MAX_RH];
static int RW, RH;

static void buf_clear(uint32_t c){
    int n = RW*RH;
    for(int i=0;i<n;i++) backbuf[i]=c;
}

static void buf_pset(int x,int y,uint32_t c){
    if((unsigned)x>=(unsigned)RW||(unsigned)y>=(unsigned)RH) return;
    backbuf[y*RW+x]=c;
}

/* blit back buffer to real framebuffer, each pixel → 2×2 block */
static void blit(void){
    for(int y=0;y<RH;y++){
        for(int x=0;x<RW;x++){
            uint32_t c = backbuf[y*RW+x];
            int sx=x*2, sy=y*2;
            fb[ sy   *FBPITCH+sx  ]=c;
            fb[ sy   *FBPITCH+sx+1]=c;
            fb[(sy+1)*FBPITCH+sx  ]=c;
            fb[(sy+1)*FBPITCH+sx+1]=c;
        }
    }
}

/* Bresenham line into back buffer */
static void rline(int x0,int y0,int x1,int y1,uint32_t c){
    int dx=x1-x0, dy=y1-y0;
    int ax=dx<0?-dx:dx, ay=dy<0?-dy:dy;
    int sx=dx<0?-1:1,   sy=dy<0?-1:1;
    int err=ax-ay;
    for(int i=0;i<512;i++){
        buf_pset(x0,y0,c);
        if(x0==x1&&y0==y1) break;
        int e2=err*2;
        if(e2>-ay){err-=ay;x0+=sx;}
        if(e2< ax){err+=ax;y0+=sy;}
    }
}

/* filled circle */
static void rfill_circle(int cx,int cy,int r,uint32_t c){
    for(int y=-r;y<=r;y++)
        for(int x=-r;x<=r;x++)
            if(x*x+y*y<=r*r)
                buf_pset(cx+x,cy+y,c);
}

/* ── 3D vector math ──────────────────────────────────────────────── */
typedef struct{ fix x,y,z; } V3;

static V3 v3add(V3 a,V3 b){ return (V3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static V3 v3sub(V3 a,V3 b){ return (V3){a.x-b.x,a.y-b.y,a.z-b.z}; }

static V3 rot_x(V3 v,int a){
    fix c=fcos(a),s=fsin(a);
    return (V3){v.x, FMUL(v.y,c)-FMUL(v.z,s), FMUL(v.y,s)+FMUL(v.z,c)};
}
static V3 rot_y(V3 v,int a){
    fix c=fcos(a),s=fsin(a);
    return (V3){FMUL(v.x,c)+FMUL(v.z,s), v.y, -FMUL(v.x,s)+FMUL(v.z,c)};
}
static V3 rot_z(V3 v,int a){
    fix c=fcos(a),s=fsin(a);
    return (V3){FMUL(v.x,c)-FMUL(v.y,s), FMUL(v.x,s)+FMUL(v.y,c), v.z};
}

/* ── camera ──────────────────────────────────────────────────────── */
static V3  cam;
static int yaw, pitch;
static fix focal;

static V3 to_cam(V3 w){
    V3 v = v3sub(w, cam);
    v = rot_y(v, -yaw);
    v = rot_x(v, -pitch);
    return v;
}

#define ZNEAR FIX(2.0f)

static int proj_x(V3 v){ return RW/2 + FINT(FDIV(FMUL(v.x,focal),v.z)); }
static int proj_y(V3 v){ return RH/2 + FINT(FDIV(FMUL(v.y,focal),v.z)); }

/* draw a 3D edge — both endpoints already in camera space */
static void edge(V3 a,V3 b,uint32_t c){
    if(a.z<ZNEAR && b.z<ZNEAR) return;

    /* simple near-clip: if one point is behind, slide it to z=ZNEAR */
    if(a.z<ZNEAR){
        fix t = FDIV(ZNEAR-a.z, b.z-a.z);
        a.x = a.x + FMUL(b.x-a.x, t);
        a.y = a.y + FMUL(b.y-a.y, t);
        a.z = ZNEAR;
    }
    if(b.z<ZNEAR){
        fix t = FDIV(ZNEAR-b.z, a.z-b.z);
        b.x = b.x + FMUL(a.x-b.x, t);
        b.y = b.y + FMUL(a.y-b.y, t);
        b.z = ZNEAR;
    }

    rline(proj_x(a),proj_y(a),proj_x(b),proj_y(b),c);
}

/* ── draw primitives ─────────────────────────────────────────────── */

/* wireframe sphere */
static void draw_sphere(V3 center,fix r,int lats,int lons,uint32_t c,
                         int rx,int ry,int rz){
    /* latitude rings */
    for(int la=1;la<lats;la++){
        int pa = la*TS/lats - TS/4;
        fix ring_y = FMUL(fsin(pa),r);
        fix ring_r = FMUL(fcos(pa),r);
        V3 prev={0,0,0}; int first=1;
        for(int lo=0;lo<=lons;lo++){
            int ya = lo*TS/lons;
            V3 lp = {FMUL(fcos(ya),ring_r), ring_y, FMUL(fsin(ya),ring_r)};
            if(rx) lp=rot_x(lp,rx);
            if(ry) lp=rot_y(lp,ry);
            if(rz) lp=rot_z(lp,rz);
            V3 wp = v3add(center,lp);
            V3 cv = to_cam(wp);
            if(!first) edge(prev,cv,c);
            prev=cv; first=0;
        }
    }
    /* longitude lines */
    for(int lo=0;lo<lons;lo++){
        int ya = lo*TS/lons;
        V3 prev={0,0,0}; int first=1;
        for(int la=0;la<=lats;la++){
            int pa = la*TS/lats - TS/4;
            fix ring_y = FMUL(fsin(pa),r);
            fix ring_r = FMUL(fcos(pa),r);
            V3 lp = {FMUL(fcos(ya),ring_r), ring_y, FMUL(fsin(ya),ring_r)};
            if(rx) lp=rot_x(lp,rx);
            if(ry) lp=rot_y(lp,ry);
            if(rz) lp=rot_z(lp,rz);
            V3 wp = v3add(center,lp);
            V3 cv = to_cam(wp);
            if(!first) edge(prev,cv,c);
            prev=cv; first=0;
        }
    }
}

/* torus — big_r = ring radius, tube_r = tube radius */
static void draw_torus(V3 center,fix big_r,fix tube_r,
                        int major_n,int minor_n,uint32_t c,
                        int rx,int ry,int rz){
    for(int ma=0;ma<=major_n;ma++){
        int a = ma*TS/major_n;
        fix ring_cx = FMUL(fcos(a), big_r);
        fix ring_cz = FMUL(fsin(a), big_r);
        V3 prev={0,0,0}; int first=1;
        for(int mi=0;mi<=minor_n;mi++){
            int b = mi*TS/minor_n;
            V3 lp;
            lp.x = FMUL(fcos(a), big_r + FMUL(fcos(b),tube_r));
            lp.y = FMUL(fsin(b), tube_r);
            lp.z = FMUL(fsin(a), big_r + FMUL(fcos(b),tube_r));
            if(rx) lp=rot_x(lp,rx);
            if(ry) lp=rot_y(lp,ry);
            if(rz) lp=rot_z(lp,rz);
            V3 wp = v3add(center,lp);
            V3 cv = to_cam(wp);
            if(!first) edge(prev,cv,c);
            prev=cv; first=0;
        }
        (void)ring_cx; (void)ring_cz;
    }
    /* major-direction rings */
    for(int mi=0;mi<minor_n;mi++){
        int b = mi*TS/minor_n;
        V3 prev={0,0,0}; int first=1;
        for(int ma=0;ma<=major_n;ma++){
            int a = ma*TS/major_n;
            V3 lp;
            lp.x = FMUL(fcos(a), big_r + FMUL(fcos(b),tube_r));
            lp.y = FMUL(fsin(b), tube_r);
            lp.z = FMUL(fsin(a), big_r + FMUL(fcos(b),tube_r));
            if(rx) lp=rot_x(lp,rx);
            if(ry) lp=rot_y(lp,ry);
            if(rz) lp=rot_z(lp,rz);
            V3 wp = v3add(center,lp);
            V3 cv = to_cam(wp);
            if(!first) edge(prev,cv,c);
            prev=cv; first=0;
        }
    }
}

/* flat circle ring (planet ring) */
static void draw_ring(V3 center,fix r,int steps,uint32_t c,int ry){
    V3 prev={0,0,0}; int first=1;
    for(int i=0;i<=steps;i++){
        int a=i*TS/steps;
        V3 lp={FMUL(fcos(a),r), 0, FMUL(fsin(a),r)};
        lp=rot_y(lp,ry);
        V3 wp=v3add(center,lp);
        V3 cv=to_cam(wp);
        if(!first) edge(prev,cv,c);
        prev=cv; first=0;
    }
}

/* ── starfield ───────────────────────────────────────────────────── */
#define NSTARS 220
static V3 stars[NSTARS];

static void init_stars(void){
    for(int i=0;i<NSTARS;i++){
        stars[i].x = FIX((int)(rnd()%1600)-800);
        stars[i].y = FIX((int)(rnd()%1200)-600);
        stars[i].z = FIX((int)(rnd()%1400)+80);
    }
}

static void draw_stars(void){
    for(int i=0;i<NSTARS;i++){
        V3 cv=to_cam(stars[i]);
        if(cv.z<ZNEAR) continue;
        int sx=proj_x(cv), sy=proj_y(cv);
        if((unsigned)sx>=(unsigned)RW||(unsigned)sy>=(unsigned)RH) continue;
        int d=FINT(cv.z);
        uint8_t br = d>600?25 : d>300?70 : 150;
        buf_pset(sx,sy,((uint32_t)br<<16)|((uint32_t)br<<8)|br);
    }
}

/* ── tiny font (digits + A-Z, 5 wide × 7 tall) ───────────────────── */
static const uint8_t FONT[36][7]={
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, // V
    {0x11,0x11,0x15,0x15,0x15,0x0A,0x0A}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
};

static void draw_glyph(int px,int py,int idx,uint32_t c){
    if(idx<0||idx>=36) return;
    for(int row=0;row<7;row++){
        uint8_t bits=FONT[idx][row];
        for(int col=4;col>=0;col--){
            if(bits&(1<<col))
                buf_pset(px+(4-col),py+row,c);
        }
    }
}

static int char_idx(char c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='A'&&c<='Z') return 10+(c-'A');
    if(c>='a'&&c<='z') return 10+(c-'a');
    return -1;
}

static void draw_str(int px,int py,const char*s,uint32_t c){
    while(*s){
        if(*s!=' ') draw_glyph(px,py,char_idx(*s),c);
        px+=6; s++;
    }
}

/* ── main ────────────────────────────────────────────────────────── */
void _start(void){
    build_trig();

    FBW   =(int)sc0(SYS_FB_WIDTH);
    FBH   =(int)sc0(SYS_FB_HEIGHT);
    FBPITCH=(int)(sc0(SYS_FB_PITCH)/4);
    fb    =(uint32_t*)sc0(SYS_MAP_FB);

    RW = FBW/2; if(RW>MAX_RW) RW=MAX_RW;
    RH = FBH/2; if(RH>MAX_RH) RH=MAX_RH;

    focal = FIX(280.0f);

    init_stars();

    /* camera */
    cam   = (V3){0, 0, 0};
    yaw   = 0;
    pitch = 0;

    /* scene objects */
    V3 station  = {FIX(  0.0f), FIX(   0.0f), FIX(320.0f)};
    V3 planet_a = {FIX(500.0f), FIX( -60.0f), FIX(700.0f)};
    V3 planet_b = {FIX(-450.0f),FIX(  50.0f), FIX(850.0f)};
    V3 small_r  = {FIX(-150.0f),FIX(  20.0f), FIX(220.0f)};

    int t=0;
    int kw=0,ks=0,ka=0,kd=0,kr=0,kf=0,kq_=0,ke=0;

    for(;;){
        /* input */
        uint8_t key=(uint8_t)sc0(SYS_POLL_KEY);
        if(key=='w'||key=='W') kw=5;
        if(key=='s'||key=='S') ks=5;
        if(key=='a'||key=='A') ka=5;
        if(key=='d'||key=='D') kd=5;
        if(key=='r'||key=='R') kr=5;
        if(key=='f'||key=='F') kf=5;
        if(key=='e'||key=='E') ke=5;
        if(key=='q'||key=='Q') kq_=5;
        if(key=='x') sc0(SYS_EXIT);

        fix mv=FIX(3.5f), sv_=FIX(2.5f);
        if(kw>0){
            cam.x+=FMUL(fsin(yaw),mv);
            cam.z+=FMUL(fcos(yaw),mv);
            kw--;
        }
        if(ks>0){
            cam.x-=FMUL(fsin(yaw),mv);
            cam.z-=FMUL(fcos(yaw),mv);
            ks--;
        }
        int ry2=(yaw+TS/4)%TS;
        if(kd>0){
            cam.x+=FMUL(fsin(ry2),sv_);
            cam.z+=FMUL(fcos(ry2),sv_);
            kd--;
        }
        if(ka>0){
            cam.x-=FMUL(fsin(ry2),sv_);
            cam.z-=FMUL(fcos(ry2),sv_);
            ka--;
        }
        if(kr>0){ cam.y-=sv_; kr--; }
        if(kf>0){ cam.y+=sv_; kf--; }
        if(ke>0){ yaw=(yaw+10)%TS;   ke--; }
        if(kq_>0){ yaw=(yaw-10+TS)%TS; kq_--; }

        /* animation */
        int st_ry  = t*3  % TS;
        int pa_ry  = t*1  % TS;
        int pb_ry  = t*2  % TS;
        int moon_a = t*6  % TS;

        /* moon orbits planet_a */
        V3 moon;
        moon.x = planet_a.x + FMUL(fcos(moon_a), FIX(90.0f));
        moon.y = planet_a.y + FMUL(fsin(moon_a/4+TS/8), FIX(18.0f));
        moon.z = planet_a.z + FMUL(fsin(moon_a), FIX(90.0f));

        /* ── render into back buffer ── */
        buf_clear(0x000000);
        draw_stars();

        /* distant sun glow */
        V3 sun={FIX(-1100.0f),FIX(-180.0f),FIX(1800.0f)};
        {
            V3 cv=to_cam(sun);
            if(cv.z>ZNEAR){
                int sx2=proj_x(cv),sy2=proj_y(cv);
                rfill_circle(sx2,sy2,5,0xFFFFCC);
                rfill_circle(sx2,sy2,8,0xFFCC4420);
                /* cross flare */
                for(int i=1;i<=30;i++){
                    uint8_t br=(uint8_t)(60-i*2);
                    uint32_t fc=((uint32_t)br<<16)|((uint32_t)(br/2)<<8);
                    buf_pset(sx2+i,sy2,fc); buf_pset(sx2-i,sy2,fc);
                    buf_pset(sx2,sy2+i,fc); buf_pset(sx2,sy2-i,fc);
                }
            }
        }

        /* planet B — gas giant with rings */
        draw_sphere(planet_b, FIX(65.0f), 7,10, 0x4455BB, 0,pb_ry,0);
        for(int ri=0;ri<3;ri++){
            fix rr = FIX(85.0f) + FIX(14.0f)*ri;
            uint8_t br=60+ri*20;
            draw_ring(planet_b,rr,28,((uint32_t)br<<16)|((uint32_t)(br/2)<<8)|(br*2),pb_ry);
        }

        /* planet A — rocky, with moon */
        draw_sphere(planet_a, FIX(48.0f), 6,8,  0x886644, 0,pa_ry,pa_ry/3);
        draw_sphere(moon,     FIX(13.0f), 4,6,  0x666655, 0,moon_a,0);

        /* small rocky asteroid */
        draw_sphere(small_r,  FIX(11.0f), 3,5,  0x554433, t*4,t*3,t*7);

        /* space station — torus + hub + arms */
        draw_torus(station, FIX(55.0f), FIX(9.0f), 16,7, 0x33BBEE, 0,st_ry,st_ry/4);
        draw_sphere(station, FIX(16.0f), 5,7, 0x1177AA, 0,st_ry,0);

        /* 4 arms */
        for(int i=0;i<4;i++){
            int arm_a=(i*TS/4+st_ry)%TS;
            V3 tip={
                station.x+FMUL(fcos(arm_a),FIX(55.0f)),
                station.y,
                station.z+FMUL(fsin(arm_a),FIX(55.0f))
            };
            edge(to_cam(station),to_cam(tip),0x225577);
            /* brace */
            int b2=(arm_a+TS/8)%TS;
            V3 brace={
                station.x+FMUL(fcos(b2),FIX(30.0f)),
                station.y+FIX(12.0f)*((i&1)?1:-1),
                station.z+FMUL(fsin(b2),FIX(30.0f))
            };
            edge(to_cam(station),to_cam(brace),0x113344);
            edge(to_cam(tip),    to_cam(brace),0x113344);
        }

        /* HUD */
        draw_str(8, 8, "WASD MOVE  EQ TURN  RF UP DN  X QUIT", 0x334433);
        /* mini compass */
        int cx2=RW-30, cy2=RH-30;
        rfill_circle(cx2,cy2,18,0x111111);
        for(int i=0;i<24;i++) buf_pset(cx2+(int)(18.0f*fcos(i*TS/24)/65536.0f),
                                        cy2+(int)(18.0f*fsin(i*TS/24)/65536.0f),0x222222);
        /* north needle */
        {
            int needle_a=(yaw+TS*3/4)%TS;
            int nx=cx2+FINT(FMUL(fcos(needle_a),FIX(14.0f)));
            int ny=cy2+FINT(FMUL(fsin(needle_a),FIX(14.0f)));
            rline(cx2,cy2,nx,ny,0x00FF88);
        }

        /* blit back buffer → framebuffer */
        blit();

        t=(t+1)%TS;
        sc1(SYS_SLEEP_MS,16);
    }
}