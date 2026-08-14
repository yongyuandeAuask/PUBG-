// src/Android_draw/draw_Gui.cpp —— 完整版（追踪.h 逻辑移植 + 全部修复，单文件）
#include "draw.h"
#include "My_font/zh_Font.h"
#include "读写.h"
#include "UeTool.h"
#include "绘图.h"
#include "nanovg.h"
#include "paradise_api.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <pthread.h>
#include <unistd.h>

extern NVGcontext* vg;
extern int g_nvg_font;
extern int g_font_agency;

static long 类地址 = 0;
static bool 忽略人机 = false;
static bool g_titleOutline = false;
static bool g_fovCircle = true;
static int RealCount = 0;
static int BotCount = 0;
static volatile int g_isFiring = 0;
static volatile int g_ads = 0;
static volatile float g_aimDs = 0;
static volatile bool g_weaponBanned = false;

// ==================== 追锁配置（追踪.h 逻辑移植）====================
static struct {
    bool enable = false;
    int  part = 0;
    int  trigger = 0;        // 0总是 1开镜 2开火 3开火或开镜
    bool lockHold = true;    // 持续锁定（追踪.h LockedTargetAddr）
    int  priority = 0;       // 0准心优先 1距离优先
    bool inBoxAim = false;   // 框内自瞄
    bool banWeapon = true;   // 武器黑名单（RPG/弓）
    bool instant = true;     // 瞬时弹道（main.cpp aimoSpeed=99999999）
    bool predict = true;
    float predScale = 1.0f;
    float dropCoef = 540.0f; // cm/s²（下坠.cpp 540*t²）
    float recoilComp = 0.0f;
    bool dynFov = false;
    bool prob = false;
    float probRate = 0.8f;
    float fov = 250.0f;
    bool gyro = false;
    float gyroStrength = 0.15f;
    bool gyroFlip = false;
    bool forceTest = false;
} g_Aim;
static struct { bool valid = false; Vector3A pos, vel; float screenDist = 0; } g_Target;
static float g_BulletSpeed = 88000.0f;   // cm/s
static volatile uint64_t g_bpHits = 0;
static volatile bool g_bpSet = false;
static volatile pid_t g_bpPid = 0;
static bool g_threadStarted = false;

// 候选池（渲染线程填，循环后选）
struct AimCand { uint64_t addr; Vector3A w, vel; float sd, wd; };
static AimCand g_cands[64]; static int g_candCount = 0;
static uint64_t g_lockAddr = 0;

static bool PtrOk(uint64_t p) { return p > 0x10000000 && p < 0x10000000000; }
static bool GetWeaponName(long obj, char* out, int outLen);
static Vector3A UnpackHFA(const paradise_hwbp_record& r, int base) {
    const __uint128_t* qs = &r.q0;
    Vector3A v;
    v.X = *(const float*)&qs[base+0];
    v.Y = *(const float*)&qs[base+1];
    v.Z = *(const float*)&qs[base+2];
    return v;
}
static void PackHFA(paradise_hwbp_record& r, int base, const Vector3A& v) {
    __uint128_t* qs = &r.q0;
    *(float*)&qs[base+0] = v.X;
    *(float*)&qs[base+1] = v.Y;
    *(float*)&qs[base+2] = v.Z;
}
static void AimFrameBegin() { g_Target.valid = false; g_Target.screenDist = 1e9f; g_candCount = 0; }
static void AimFeedBulletSpeed(float v) {
    if (v > 50.0f && v < 2000.0f) v *= 100.0f;  // m/s → cm/s
    if (v > 50.0f) g_BulletSpeed = v;
}

// ===== 选靶+弹道（追踪.h：锁定优先/优先级/预判*强度*distFactor/下坠）=====
static void SelectAimTarget() {
    g_Target.valid = false;
    if (g_candCount <= 0) { g_lockAddr = 0; return; }
    int best = -1; float bestVal = 1e9f;
    for (int i = 0; i < g_candCount; i++) {
        float val = (g_Aim.priority == 1) ? g_cands[i].wd : g_cands[i].sd;
        if (g_Aim.lockHold && g_lockAddr && g_cands[i].addr == g_lockAddr) val = 0.01f;
        if (val > 0 && val < bestVal) { bestVal = val; best = i; }
    }
    if (best < 0) { g_lockAddr = 0; return; }
    g_lockAddr = g_cands[best].addr;
    Vector3A aimP = g_cands[best].w;
    float distM = g_cands[best].wd;
    float fly = g_Aim.instant ? 0.0f : distM / (g_BulletSpeed * 0.01f);  // 秒
    float distFactor = 1.0f;
    if (distM > 100.0f) { distFactor = 1.0f - (distM - 100.0f) * 0.002f; if (distFactor < 0.4f) distFactor = 0.4f; }
    if (!g_Aim.instant && g_Aim.predict && fly > 0.0f) {
        aimP.X += g_cands[best].vel.X * fly * g_Aim.predScale * distFactor;
        aimP.Y += g_cands[best].vel.Y * fly * g_Aim.predScale * distFactor;
        aimP.Z += g_cands[best].vel.Z * fly * g_Aim.predScale * distFactor;
    }
    if (!g_Aim.instant) aimP.Z += g_Aim.dropCoef * fly * fly;
    g_Target.pos = aimP; g_Target.vel = g_cands[best].vel;
    g_Target.screenDist = g_cands[best].sd; g_Target.valid = true;
}

// ==================== 追锁线程（修复：无条件写回）====================
static void* HwbpAimThread(void*) {
    while (true) {
        if (!初始化 || !libbase || !MySelf) { usleep(50000); continue; }
        if (!g_Aim.enable) { if (g_bpSet){driver->hwbp_remove();g_bpSet=false;} usleep(50000); continue; }
        if (g_bpSet && g_bpPid != pid) { driver->hwbp_remove(); g_bpSet = false; }
        if (!g_bpSet) {
            paradise_hwbp_point_config pt; memset(&pt,0,sizeof(pt));
            pt.address=(uint64_t)libbase+0x6DFE100;
            pt.type=PARADISE_HWBP_EXECUTE;
            pt.length=4;
            pt.scope=PARADISE_HWBP_ALL_THREADS;
            g_bpSet = driver->hwbp_set(&pt,1);
            if (g_bpSet) g_bpPid = pid;
            if(!g_bpSet){usleep(100000);continue;}
        }
        // 触发规则（追踪.h shouldAim）
        bool shouldAim = false;
        switch (g_Aim.trigger) {
            case 0: shouldAim = true; break;
            case 1: shouldAim = (g_ads != 0); break;
            case 2: shouldAim = (g_isFiring != 0); break;
            default: shouldAim = (g_isFiring != 0) || (g_ads != 0); break;
        }
        if (g_Aim.banWeapon && g_weaponBanned) shouldAim = false;
        if (!shouldAim) g_lockAddr = 0;

        if (g_Aim.gyro && shouldAim && g_Target.valid) {
            float c = matrix[3]*g_Target.pos.X+matrix[7]*g_Target.pos.Y+matrix[11]*g_Target.pos.Z+matrix[15];
            if (c>0.001f){
                float sx = px+(matrix[0]*g_Target.pos.X+matrix[4]*g_Target.pos.Y+matrix[8]*g_Target.pos.Z+matrix[12])/c*px;
                float sy = py-(matrix[1]*g_Target.pos.X+matrix[5]*g_Target.pos.Y+matrix[9]*g_Target.pos.Z+matrix[13])/c*py;
                g_aimDs = sqrtf(powf(px-sx,2)+powf(py-sy,2));
                float gx=(sx-px)*g_Aim.gyroStrength, gy=(sy-py)*g_Aim.gyroStrength;
                if(g_Aim.gyroFlip){gx=-gx;gy=-gy;}
                driver->gyro_update(gx,gy);
            }
        }

        paradise_hwbp_record recs[8]; uint32_t cnt=0;
        if (driver->hwbp_get_records(0,0,recs,8,&cnt) && cnt>0){
            for (uint32_t i=0;i<cnt;i++){
                g_bpHits++;
                auto& rec = recs[i];
                memset(rec.mask,0,sizeof(rec.mask));
                bool wantWrite = shouldAim && g_Target.valid;
                if (wantWrite && g_Aim.prob && ((float)rand()/(float)RAND_MAX)>g_Aim.probRate) wantWrite=false;
                if (wantWrite){
                    Vector3A start = UnpackHFA(rec,0);
                    if (fabsf(start.X)<1.0f && fabsf(start.Y)<1.0f) wantWrite=false;
                    else {
                        Vector3A aim = g_Target.pos;
                        if (g_Aim.recoilComp>0.0f && g_isFiring){
                            float d0 = sqrtf(powf(aim.X-start.X,2)+powf(aim.Y-start.Y,2)+powf(aim.Z-start.Z,2));
                            aim.Z -= (d0/100.0f)*g_Aim.recoilComp;
                        }
                        float dx=aim.X-start.X, dy=aim.Y-start.Y, dz=aim.Z-start.Z;
                        float hyp=sqrtf(dx*dx+dy*dy);
                        if (hyp < 0.001f) wantWrite=false;
                        else {
                            Vector3A rot;
                            rot.X=atan2f(dz,hyp)*(180.0f/3.14159265f);
                            if(rot.X<-75)rot.X=-75; if(rot.X>75)rot.X=75;
                            rot.Y=atan2f(dy,dx)*(180.0f/3.14159265f);
                            while(rot.Y<-180)rot.Y+=360; while(rot.Y>180)rot.Y-=360;
                            rot.Z=0;
                            if (g_Aim.forceTest){rot.X=60;rot.Z=0;}
                            PackHFA(rec,3,rot);
                            PARADISE_BP_SET_MASK(&rec,PARADISE_REG_Q3,PARADISE_BP_OP_WRITE);
                            PARADISE_BP_SET_MASK(&rec,PARADISE_REG_Q4,PARADISE_BP_OP_WRITE);
                            PARADISE_BP_SET_MASK(&rec,PARADISE_REG_Q5,PARADISE_BP_OP_WRITE);
                        }
                    }
                }
                // ★ 致命修复：无论改没改都必须写回，游戏线程才能恢复
                driver->hwbp_set_record(0,i,&rec);
            }
            driver->hwbp_clear_records(0);
        }
        usleep(200);
    }
    return nullptr;
}

void DrawInit() {
    pid_t newPid = getPID("com.rekoo.pubgm");
    if (newPid <= 0){printf("游戏未启动\n");return;}
    long newBase = getModuleBase("libUE4.so");
    if (newBase <= 0){printf("libUE4.so 未找到\n");return;}
    bool firstTime = !初始化;
    bool changed = (newPid != pid) || (newBase != libbase);
    pid = newPid;
    libbase = newBase;
    driver->initialize(pid);
    if (changed && !firstTime) { driver->hwbp_remove(); g_bpSet=false; g_bpPid=0; MySelf=0; g_lockAddr=0; }
    if (firstTime) {
        初始化 = true;
        if (!g_threadStarted) {
            pthread_t t; pthread_create(&t,nullptr,HwbpAimThread,nullptr); pthread_detach(t);
            g_threadStarted = true;
        }
    }
    printf("初始化成功! libUE4: %lx, pid: %d\n", libbase, pid);
}

void UpdateGameData() {
    if (!初始化) return;
    Matrix = driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1d5f70) + 0xC0) + 0x590;
    Uworld = driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1fb900) + 0x810) + 0x78);
    Uleve = driver->read<uint64_t>(Uworld + 0x30);
    Arrayaddr = driver->read<uint64_t>(Uleve + 0xA0);
    Count = driver->read<int>(Uleve + 0xA8);
    MySelf = driver->read<uint64_t>(
        driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(
            driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1fb900) + 0x810)
            + 0x78) + 0x38) + 0x78) + 0x30) + 0x28c8;
    类地址 = driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xec73720) + 0x110);
    memset(matrix,0,16);
    driver->read((uintptr_t)Matrix, matrix, 16*4);
    g_isFiring = driver->read<int>(MySelf + 0x1830);
    g_ads = driver->read<int>(MySelf + 0x1134);
    {
        static int wTick = 0;
        if ((wTick++ & 31) == 0) {
            char wn[48]="";
            long a=driver->read<uint64_t>(MySelf + 0x2608);
            long b=PtrOk((uint64_t)a)?driver->read<uint64_t>(a + 0x5D8):0;
            long c=PtrOk((uint64_t)b)?driver->read<uint64_t>(b + 0x1e0):0;
            if (PtrOk((uint64_t)c)) GetWeaponName(c, wn, sizeof(wn));
            g_weaponBanned = (strstr(wn,"RPG")||strstr(wn,"Bow")||strstr(wn,"Explosive"));
        }
    }
    long wq1 = driver->read<uint64_t>(MySelf + 0x2608);
    if (PtrOk((uint64_t)wq1)) {
        long wq2 = driver->read<uint64_t>(wq1 + 0x5D8);
        if (PtrOk((uint64_t)wq2)) {
            float bs = driver->read<float>(wq2 + 0x560);
            AimFeedBulletSpeed(bs);
        }
    }
}

static void SetFontNVG(NVGcontext* vg, int fontId){ nvgFontFaceId(vg,fontId); if(fontId==g_font_agency&&g_nvg_font>=0)nvgAddFallbackFont(vg,"agency","zh"); }
static float UI_SCALE(){ return (float)abs_ScreenY/1080.0f; }
static void TextDrawScaled(NVGcontext* vg,float x,float y,float sx,const char* t){ nvgSave(vg);nvgTranslate(vg,x,y);nvgScale(vg,sx,1.0f);nvgText(vg,0,0,t,NULL);nvgRestore(vg); }
static void DrawSoftTextNVG(NVGcontext* vg,int fontId,const char* text,Vector2A pos,float fontSize,NVGcolor color,bool isCenter=true){
    if(!vg||!text||fontId<0)return;
    float fs=fontSize*UI_SCALE(); nvgFontSize(vg,fs); SetFontNVG(vg,fontId);
    nvgTextLetterSpacing(vg,fs*0.02f);
    nvgTextAlign(vg,(isCenter?NVG_ALIGN_CENTER:NVG_ALIGN_LEFT)|NVG_ALIGN_MIDDLE);
    float y=pos.Y+fs*0.06f;
    nvgFontBlur(vg,4.0f); nvgFillColor(vg,nvgRGBA(0,0,0,180));
    TextDrawScaled(vg,pos.X,y+fs*0.06f,1.06f,text);
    nvgFontBlur(vg,0);
    float o=fs*0.04f+0.6f;
    nvgFillColor(vg,nvgRGBA(0,0,0,255));
    TextDrawScaled(vg,pos.X-o,y-o,1.06f,text); TextDrawScaled(vg,pos.X+o,y-o,1.06f,text);
    TextDrawScaled(vg,pos.X-o,y+o,1.06f,text); TextDrawScaled(vg,pos.X+o,y+o,1.06f,text);
    nvgFillColor(vg,color); TextDrawScaled(vg,pos.X,y,1.06f,text);
    nvgTextLetterSpacing(vg,0);
}
void DrawOutlinedTextNVG(NVGcontext* vg,int fontId,const char* text,Vector2A pos,float fontSize,NVGcolor color,NVGcolor outlineColor,bool isCenter=false,float outlineWidth=1.0f){
    if(!vg||!text||fontId<0)return;
    nvgFontSize(vg,fontSize); SetFontNVG(vg,fontId);
    nvgTextAlign(vg,(isCenter?NVG_ALIGN_CENTER:NVG_ALIGN_LEFT)|NVG_ALIGN_TOP);
    nvgFillColor(vg,outlineColor);
    const float o=outlineWidth;
    nvgText(vg,pos.X-o,pos.Y-o,text,NULL); nvgText(vg,pos.X+o,pos.Y-o,text,NULL);
    nvgText(vg,pos.X-o,pos.Y+o,text,NULL); nvgText(vg,pos.X+o,pos.Y+o,text,NULL);
    nvgFillColor(vg,color); nvgText(vg,pos.X,pos.Y,text,NULL);
}
static void DrawLineNVG(NVGcontext* vg,float x1,float y1,float x2,float y2,NVGcolor color,float thickness=1.5f){
    if(!vg)return; nvgBeginPath(vg); nvgMoveTo(vg,x1,y1); nvgLineTo(vg,x2,y2);
    nvgStrokeColor(vg,color); nvgStrokeWidth(vg,thickness); nvgStroke(vg);
}
static void DrawHexagonStarNVG(NVGcontext* vg,float x,float y,float size,float rotation,float thickness=1.5f){
    if(!vg)return;
    NVGcolor white=nvgRGBA(255,255,255,255), black=nvgRGBA(0,0,0,255);
    float ptx[6],pty[6];
    for(int i=0;i<6;i++){float a=rotation+2.0f*NVG_PI*i/6.0f; ptx[i]=x+size*cosf(a); pty[i]=y+size*sinf(a);}
    float bt=thickness+2.0f;
    DrawLineNVG(vg,ptx[0],pty[0],ptx[2],pty[2],black,bt); DrawLineNVG(vg,ptx[2],pty[2],ptx[4],pty[4],black,bt);
    DrawLineNVG(vg,ptx[4],pty[4],ptx[0],pty[0],black,bt); DrawLineNVG(vg,ptx[1],pty[1],ptx[3],pty[3],black,bt);
    DrawLineNVG(vg,ptx[3],pty[3],ptx[5],pty[5],black,bt); DrawLineNVG(vg,ptx[5],pty[5],ptx[1],pty[1],black,bt);
    DrawLineNVG(vg,ptx[0],pty[0],ptx[2],pty[2],white,thickness); DrawLineNVG(vg,ptx[2],pty[2],ptx[4],pty[4],white,thickness);
    DrawLineNVG(vg,ptx[4],pty[4],ptx[0],pty[0],white,thickness); DrawLineNVG(vg,ptx[1],pty[1],ptx[3],pty[3],white,thickness);
    DrawLineNVG(vg,ptx[3],pty[3],ptx[5],pty[5],white,thickness); DrawLineNVG(vg,ptx[5],pty[5],ptx[1],pty[1],white,thickness);
}
static void DrawLogoNVG(NVGcontext* vg,float x,float y,float size){ static float r=0; r+=0.05f; DrawHexagonStarNVG(vg,x,y,size,r,1.5f); }
static bool GetWeaponName(long obj,char* out,int outLen){
    if(!PtrOk((uint64_t)obj))return false;
    int wID=driver->read<int>(obj+24);
    long wEntry=driver->read<uint64_t>(driver->read<uint64_t>(类地址+(wID/0x4000)*0x8)+(wID%0x4000)*0x8);
    if(!PtrOk((uint64_t)wEntry))return false;
    char wName[64]=""; driver->read((uintptr_t)(wEntry+0xC),wName,64);
    char* p=strstr(wName,"Weap"); if(!p)p=strstr(wName,"BP_"); if(!p)p=strstr(wName,"Gun"); if(!p)return false;
    strncpy(out,p,outLen-1); out[outLen-1]=0;
    char* us=strstr(out,"_C"); if(us)*us=0;
    return out[0]!=0;
}

void DrawPlayerNVG(NVGcontext* vg) {
    if (!初始化 || MySelf==0 || vg==nullptr) return;
    int 自己队伍=driver->read<int>(MySelf+0x998);
    Vector3A Z; driver->read((uintptr_t)(driver->read<uint64_t>(MySelf+0x208)+0x1c8), &Z, sizeof(Z));
    PlayerCount=0; RealCount=0; BotCount=0;
    AimFrameBegin();
    for (int i=0;i<Count;i++){
        long int Objaddr=driver->read<uint64_t>(Arrayaddr+0x8*i);
        if(Objaddr<=0xffff||Objaddr==0||Objaddr<=0x10000000||Objaddr%4!=0||Objaddr>=0x10000000000)continue;
        if(MySelf==Objaddr)continue;
        int ClassID=driver->read<int>(Objaddr+24);
        long FNameEntry=driver->read<uint64_t>(driver->read<uint64_t>(类地址+(ClassID/0x4000)*0x8)+(ClassID%0x4000)*0x8);
        char ClassName[64]=""; driver->read((uintptr_t)(FNameEntry+0xC),ClassName,64);
        if(strstr(ClassName,"BPPawn_Escape_Raven")!=0||strstr(ClassName,"BPPawn_Escape_UAV_C")!=0)continue;
        if (DrawIo[8]) {
            const char* vname=NULL;
            if(strstr(ClassName,"UAZ"))vname="吉普车"; else if(strstr(ClassName,"Dacia"))vname="轿车";
            else if(strstr(ClassName,"Buggy"))vname="蹦蹦车"; else if(strstr(ClassName,"Mirado"))vname="跑车";
            else if(strstr(ClassName,"Rony"))vname="越野皮卡"; else if(strstr(ClassName,"PickUp"))vname="皮卡车";
            else if(strstr(ClassName,"MiniBus"))vname="迷你巴士"; else if(strstr(ClassName,"PG117"))vname="快艇";
            else if(strstr(ClassName,"AquaRail"))vname="摩托艇"; else if(strstr(ClassName,"VH_Motorcycle"))vname="摩托车";
            else if(strstr(ClassName,"VH_Snowmobile")||strstr(ClassName,"VH_Snowbike"))vname="雪地摩托";
            else if(strstr(ClassName,"VH_Scooter")||strstr(ClassName,"Scooter"))vname="踏板车";
            else if(strstr(ClassName,"VH_ATV1"))vname="地形车"; else if(strstr(ClassName,"VH_UTV"))vname="UTV";
            else if(strstr(ClassName,"VH_BRDM"))vname="装甲车"; else if(strstr(ClassName,"VH_4SportCar"))vname="敞篷跑车";
            else if(strstr(ClassName,"_CoupeRB_"))vname="双人跑车"; else if(strstr(ClassName,"Bigfoot"))vname="大脚车";
            else if(strstr(ClassName,"TrackVehicle"))vname="地铁矿车"; else if(strstr(ClassName,"BP_VH_Tuk_"))vname="三轮摩托";
            else if(strstr(ClassName,"LadaNiva"))vname="雪地越野车"; else if(strstr(ClassName,"AirDropPlane"))vname="空投飞机";
            else if(strstr(ClassName,"VH_Motorglider")||strstr(ClassName,"wing_"))vname="飞行器";
            else if(strstr(ClassName,"Horse"))vname="马"; else if(strstr(ClassName,"Bike"))vname="自行车";
            if(vname){
                long int vptr=driver->read<uint64_t>(Objaddr+0x208);
                if(PtrOk((uint64_t)vptr)){
                    Vector3A V; driver->read((uintptr_t)(vptr+0x1c8),&V,sizeof(V));
                    float vc=matrix[3]*V.X+matrix[7]*V.Y+matrix[11]*V.Z+matrix[15];
                    if(vc>0.001f){
                        float vDist=sqrt(pow(V.X-Z.X,2)+pow(V.Y-Z.Y,2)+pow(V.Z-Z.Z,2))*0.01f;
                        if(vDist<=400){
                            float vx=px+(matrix[0]*V.X+matrix[4]*V.Y+matrix[8]*V.Z+matrix[12])/vc*px;
                            float vy=py-(matrix[1]*V.X+matrix[5]*V.Y+matrix[9]*V.Z+matrix[13])/vc*py;
                            if(vx>0&&vx<(float)abs_ScreenX&&vy>0&&vy<(float)abs_ScreenY){
                                char vbuf[64]; snprintf(vbuf,sizeof(vbuf),"%s [%dm]",vname,(int)vDist);
                                DrawOutlinedTextNVG(vg,g_font_agency,vbuf,{vx,vy-20},20.0f,nvgRGBA(255,255,0,255),nvgRGBA(0,0,0,255),true,1.0f);
                            }
                        }
                    }
                }
                continue;
            }
        }
        float 玩家标志=driver->read<float>(Objaddr+0x2b78);
        bool isDog=(strstr(ClassName,"AIMob_PatrolDog_C")!=0);
        bool isHunger=(strstr(ClassName,"BPPawn_HungerH_C")!=0)||(strstr(ClassName,"BPPawn_HungerB_C")!=0);
        if(玩家标志!=479.5f&&!isDog&&!isHunger)continue;
        int 状态=driver->read<int>(Objaddr+0x1058);
        if(状态==1048592||状态==1048576)continue;
        int 敌人队伍=driver->read<int>(Objaddr+0x998);
        if(敌人队伍==自己队伍)continue;
        int botFlag=driver->read<int>(Objaddr+0xa59);
        bool isBot=isDog||isHunger||(敌人队伍!=0&&(botFlag==16842753||botFlag==16843009||botFlag==16843008));
        if(忽略人机&&isBot)continue;
        float 当前血量=driver->read<float>(Objaddr+0xe60);
        float 最大血量=driver->read<float>(Objaddr+0xe64);
        if(最大血量<=0)continue;
        PlayerCount++; if(isBot)BotCount++; else RealCount++;
        Vector3A D; driver->read((uintptr_t)(driver->read<uint64_t>(Objaddr+0x208)+0x1c8),&D,sizeof(D));
        float camera=matrix[3]*D.X+matrix[7]*D.Y+matrix[11]*D.Z+matrix[15];
        if(camera<=0.001f)continue;
        float Distance=sqrt(pow(D.X-Z.X,2)+pow(D.Y-Z.Y,2)+pow(D.Z-Z.Z,2))*0.01f;
        if(Distance>500||Distance<=0)continue;
        float r_x=px+(matrix[0]*D.X+matrix[4]*D.Y+matrix[8]*D.Z+matrix[12])/camera*px;
        float r_y=py-(matrix[1]*D.X+matrix[5]*D.Y+matrix[9]*(D.Z-5)+matrix[13])/camera*py;
        float r_w=py-(matrix[1]*D.X+matrix[5]*D.Y+matrix[9]*(D.Z+205)+matrix[13])/camera*py;
        float W=(r_y-r_w)/2;
        if(W<=0||W>3000)continue;
        // 框内自瞄（追踪.h）：准心不在框内 → 不收候选
        if (g_Aim.inBoxAim) {
            float bL=r_x-W*0.5f, bR=r_x+W*0.5f, bT=r_y-W, bB=r_y+W;
            if (!(px>=bL&&px<=bR&&py>=bT&&py<=bB)) continue;
        }
        float gL=r_x-W*0.5f, gR=r_x+W*0.5f;
        float gT=r_y-W,      gB=r_y+W;
        bool inView = gR>0.0f && gL<(float)abs_ScreenX && gB>0.0f && gT<(float)abs_ScreenY;
        if(!inView) continue;
        float MIDDLE=r_x, TOP_FALLBACK=r_y-W, BOTTOM_FALLBACK=r_y+W;
        Vector2A Head,Chest,Pelvis,Left_Shoulder,Right_Shoulder,Left_Elbow,Right_Elbow,
                 Left_Wrist,Right_Wrist,Left_Thigh,Right_Thigh,Left_Knee,Right_Knee,Left_Ankle,Right_Ankle;
        bool bonesOk=false;
        Vector3A wHeadW,wChestW,wPelvisW;
        if (DrawIo[4]||g_Aim.enable) {
            long int Mesh=driver->read<uint64_t>(Objaddr+0x510);
            if(PtrOk((uint64_t)Mesh)){
                long int boneArrayPtr=driver->read<uint64_t>(Mesh+0x9a8);
                int BoneCount=driver->read<int>(Mesh+0x9a8+8);
                if(PtrOk((uint64_t)boneArrayPtr)&&BoneCount>0&&BoneCount<200){
                    int idx_head=5,idx_chest=4,idx_pelvis=0;
                    int idx_lsh,idx_rsh,idx_lelb,idx_relb,idx_lw,idx_rw,idx_lth,idx_rth,idx_lk,idx_rk,idx_la,idx_ra;
                    if(isDog){idx_lsh=7;idx_rsh=11;idx_lelb=8;idx_relb=12;idx_lw=9;idx_rw=13;idx_lth=14;idx_rth=18;idx_lk=15;idx_rk=19;idx_la=16;idx_ra=20;}
                    else if(isHunger){idx_lsh=11;idx_rsh=18;idx_lelb=12;idx_relb=19;idx_lw=13;idx_rw=20;idx_lth=24;idx_rth=29;idx_lk=25;idx_rk=30;idx_la=26;idx_ra=31;}
                    else if(BoneCount==67){idx_lsh=13;idx_rsh=34;idx_lelb=14;idx_relb=35;idx_lw=16;idx_rw=37;idx_lth=54;idx_rth=58;idx_lk=55;idx_rk=59;idx_la=56;idx_ra=60;}
                    else if(BoneCount==29){idx_lsh=7;idx_rsh=13;idx_lelb=8;idx_relb=14;idx_lw=9;idx_rw=15;idx_lth=18;idx_rth=21;idx_lk=19;idx_rk=22;idx_la=20;idx_ra=23;}
                    else{idx_lsh=11;idx_rsh=32;idx_lelb=12;idx_relb=33;idx_lw=63;idx_rw=62;idx_lth=52;idx_rth=56;idx_lk=53;idx_rk=57;idx_la=54;idx_ra=58;}
                    long int human=Mesh+0x210; long int Bone=boneArrayPtr+0x30;
                    FTransform meshtrans=getBone(human);
                    FMatrix c2wMatrix=TransformToMatrix(meshtrans);
                    auto BW=[&](int idx){return MarixToVector(MatrixMulti(TransformToMatrix(getBone(Bone+idx*48)),c2wMatrix));};
                    wHeadW=BW(idx_head); wHeadW.Z+=7.0f;
                    wChestW=BW(idx_chest); wPelvisW=BW(idx_pelvis);
                    Vector3A wLSh=BW(idx_lsh),wRSh=BW(idx_rsh),wLElb=BW(idx_lelb),wRElb=BW(idx_relb);
                    Vector3A wLW=BW(idx_lw),wRW=BW(idx_rw),wLTh=BW(idx_lth),wRTh=BW(idx_rth);
                    Vector3A wLK=BW(idx_lk),wRK=BW(idx_rk),wLA=BW(idx_la),wRA=BW(idx_ra);
                    auto okw=[&](const Vector3A& p){return fabsf(p.X-wPelvisW.X)<300&&fabsf(p.Y-wPelvisW.Y)<300&&fabsf(p.Z-wPelvisW.Z)<300;};
                    if(okw(wHeadW)&&okw(wChestW)&&okw(wLSh)&&okw(wRSh)&&okw(wLElb)&&okw(wRElb)&&okw(wLW)&&okw(wRW)&&
                       okw(wLTh)&&okw(wRTh)&&okw(wLK)&&okw(wRK)&&okw(wLA)&&okw(wRA)){
                        Head=WorldToScreen(wHeadW,matrix,camera); Chest=WorldToScreen(wChestW,matrix,camera);
                        Pelvis=WorldToScreen(wPelvisW,matrix,camera);
                        Left_Shoulder=WorldToScreen(wLSh,matrix,camera); Right_Shoulder=WorldToScreen(wRSh,matrix,camera);
                        Left_Elbow=WorldToScreen(wLElb,matrix,camera); Right_Elbow=WorldToScreen(wRElb,matrix,camera);
                        Left_Wrist=WorldToScreen(wLW,matrix,camera); Right_Wrist=WorldToScreen(wRW,matrix,camera);
                        Left_Thigh=WorldToScreen(wLTh,matrix,camera); Right_Thigh=WorldToScreen(wRTh,matrix,camera);
                        Left_Knee=WorldToScreen(wLK,matrix,camera); Right_Knee=WorldToScreen(wRK,matrix,camera);
                        Left_Ankle=WorldToScreen(wLA,matrix,camera); Right_Ankle=WorldToScreen(wRA,matrix,camera);
                        bonesOk=true;
                        float bh=((Left_Ankle.Y<Right_Ankle.Y)?Right_Ankle.Y:Left_Ankle.Y)-(Head.Y-W/5.0f);
                        if(bh>6.0f*W||bh<0.5f*W)bonesOk=false;
                        if(bonesOk){
                            Vector2A partSc=(g_Aim.part==0)?Head:((g_Aim.part==1)?Chest:Pelvis);
                            Vector3A partW=(g_Aim.part==0)?wHeadW:((g_Aim.part==1)?wChestW:wPelvisW);
                            float sd=sqrtf(powf(partSc.X-px,2)+powf(partSc.Y-py,2));
                            if(sd<g_Aim.fov && g_candCount<64){
                                Vector3A vel; memset(&vel,0,sizeof(vel));
                                long vp=driver->read<uint64_t>(Objaddr+0x208);
                                if(PtrOk((uint64_t)vp))driver->read(vp+0x2C0,&vel,sizeof(vel));
                                AimCand& c=g_cands[g_candCount++];
                                c.addr=(uint64_t)Objaddr; c.w=partW; c.vel=vel; c.sd=sd; c.wd=Distance;
                            }
                        }
                    }
                }
            }
        }
        float headX=(bonesOk&&Head.X>0)?Head.X:MIDDLE;
        float left=headX-W*0.6f, right=headX+W*0.6f;
        float top=(bonesOk&&Head.Y>0)?(Head.Y-W/5.0f):TOP_FALLBACK;
        float bottom=(bonesOk)?((Left_Ankle.Y<Right_Ankle.Y)?Right_Ankle.Y+W/10.0f:Left_Ankle.Y+W/10.0f):BOTTOM_FALLBACK;
        NVGcolor COL_WHITE=nvgRGBA(255,255,255,255), COL_BLACK=nvgRGBA(0,0,0,255);
        if(DrawIo[3])DrawLineNVG(vg,(float)abs_ScreenX*0.5f,73.0f,headX,top,COL_WHITE,1.0f);
        if(DrawIo[1]){
            DrawLineNVG(vg,left,top,right,top,COL_WHITE,1.5f); DrawLineNVG(vg,right,top,right,bottom,COL_WHITE,1.5f);
            DrawLineNVG(vg,right,bottom,left,bottom,COL_WHITE,1.5f); DrawLineNVG(vg,left,bottom,left,top,COL_WHITE,1.5f);
        }
        if(DrawIo[4]&&bonesOk){
            nvgBeginPath(vg); nvgCircle(vg,Head.X,Head.Y,W/5.0f);
            nvgStrokeColor(vg,COL_WHITE); nvgStrokeWidth(vg,1.5f); nvgStroke(vg);
            DrawLineNVG(vg,Head.X,Head.Y,Chest.X,Chest.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Chest.X,Chest.Y,Pelvis.X,Pelvis.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Chest.X,Chest.Y,Left_Shoulder.X,Left_Shoulder.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Chest.X,Chest.Y,Right_Shoulder.X,Right_Shoulder.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Left_Shoulder.X,Left_Shoulder.Y,Left_Elbow.X,Left_Elbow.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Right_Shoulder.X,Right_Shoulder.Y,Right_Elbow.X,Right_Elbow.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Left_Elbow.X,Left_Elbow.Y,Left_Wrist.X,Left_Wrist.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Right_Elbow.X,Right_Elbow.Y,Right_Wrist.X,Right_Wrist.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Pelvis.X,Pelvis.Y,Left_Thigh.X,Left_Thigh.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Pelvis.X,Pelvis.Y,Right_Thigh.X,Right_Thigh.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Left_Thigh.X,Left_Thigh.Y,Left_Knee.X,Left_Knee.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Right_Thigh.X,Right_Thigh.Y,Right_Knee.X,Right_Knee.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Left_Knee.X,Left_Knee.Y,Left_Ankle.X,Left_Ankle.Y,COL_WHITE,1.5f);
            DrawLineNVG(vg,Right_Knee.X,Right_Knee.Y,Right_Ankle.X,Right_Ankle.Y,COL_WHITE,1.5f);
        }
        if(DrawIo[2]){
            char distBuf[16]; snprintf(distBuf,sizeof(distBuf),"%d m",(int)Distance);
            float yPos=bottom+6; if(yPos>(float)abs_ScreenY-25)yPos=(float)abs_ScreenY-25;
            DrawOutlinedTextNVG(vg,g_font_agency,distBuf,{headX,yPos},25.0f,COL_WHITE,COL_BLACK,true,1.0f);
        }
        if(DrawIo[5]){
            char tagBuf[96];
            if(isBot)snprintf(tagBuf,sizeof(tagBuf),"[%d] 人机",敌人队伍);
            else{getUTF8(PlayerName,driver->read<uint64_t>(Objaddr+0x960));snprintf(tagBuf,sizeof(tagBuf),"[%d] %s",敌人队伍,PlayerName);}
            float yPos=top-24; if(yPos<0)yPos=0;
            DrawOutlinedTextNVG(vg,g_font_agency,tagBuf,{headX,yPos},18.0f,COL_WHITE,COL_BLACK,true,1.0f);
        }
        if(DrawIo[9]){
            char clean[48]="";
            long wq1=driver->read<uint64_t>(Objaddr+0x2608);
            if(PtrOk((uint64_t)wq1)){
                long wq2=driver->read<uint64_t>(wq1+0x5D8);
                long cands[3]={0,0,0};
                if(PtrOk((uint64_t)wq2)){
                    cands[0]=wq2;
                    cands[1]=driver->read<uint64_t>(wq2+0x1e0);
                    long ent=driver->read<uint64_t>(wq1+0x1370);
                    if(PtrOk((uint64_t)ent)){
                        long cw=driver->read<uint64_t>(ent+0x5D8);
                        if(PtrOk((uint64_t)cw))cands[2]=driver->read<uint64_t>(cw+0x1e0);
                    }
                }
                for(int c=0;c<3&&!clean[0];c++){
                    if(PtrOk((uint64_t)cands[c]))GetWeaponName(cands[c],clean,sizeof(clean));
                }
            }
            if(clean[0]){
                float wy=top-44; if(wy<0)wy=0;
                DrawOutlinedTextNVG(vg,g_font_agency,clean,{headX,wy},16.0f,COL_WHITE,COL_BLACK,true,1.0f);
            }
        }
        if(DrawIo[7]){
            char buf[32]; sprintf(buf,"0x%lx",Objaddr);
            DrawOutlinedTextNVG(vg,g_font_agency,buf,{headX,bottom},16.0f,COL_WHITE,COL_BLACK,true,1.0f);
        }
        if(DrawIo[6]){
            float CurHP=当前血量, MaxHP=最大血量;
            if(MaxHP<=0)MaxHP=100.0f;
            if(CurHP<0)CurHP=0; if(CurHP>MaxHP)CurHP=MaxHP;
            int hp_percent=(int)(CurHP/MaxHP*100);
            float cx2=headX, cy2=top-56.0f;
            nvgBeginPath(vg);
            nvgArc(vg,cx2,cy2,24.0f,-NVG_PI/2.0f,-NVG_PI/2.0f+2.0f*NVG_PI*(CurHP/MaxHP),NVG_CW);
            nvgStrokeColor(vg,COL_WHITE); nvgStrokeWidth(vg,4.0f); nvgLineCap(vg,NVG_ROUND); nvgStroke(vg);
            char hpBuf[8]; snprintf(hpBuf,sizeof(hpBuf),"%d%%",hp_percent);
            DrawOutlinedTextNVG(vg,g_font_agency,hpBuf,{cx2,cy2-6.0f},13.0f,COL_WHITE,COL_BLACK,true,1.0f);
        }
    }
    SelectAimTarget();
}

void DrawCanvas() {
    if(!vg)return;
    nvgBeginFrame(vg,(float)abs_ScreenX,(float)abs_ScreenY,1.0f);
    float cx=(float)abs_ScreenX*0.5f;
    DrawPlayerNVG(vg);
    if(g_fovCircle&&g_Aim.enable){
        float r=g_Aim.fov;
        if(g_Aim.dynFov&&g_isFiring&&g_Target.valid&&g_aimDs<1e8f)r=g_aimDs;
        if(r<20)r=20;
        nvgBeginPath(vg); nvgCircle(vg,px,py,r);
        nvgStrokeColor(vg,nvgRGBA(255,140,0,255)); nvgStrokeWidth(vg,2.0f); nvgStroke(vg);
    }
    DrawLogoNVG(vg,(float)abs_ScreenX/4.0f,(float)abs_ScreenY/10.0f,35.0f);
    if(g_titleOutline){
        DrawOutlinedTextNVG(vg,g_font_agency,"Asuka追踪 @Asuka1314",{cx,40.0f*UI_SCALE()},40.0f*UI_SCALE(),nvgRGBA(255,255,255,255),nvgRGBA(0,0,0,255),true,2.0f);
        char infoBuf[64]; snprintf(infoBuf,sizeof(infoBuf),"真人: %d  人机: %d",RealCount,BotCount);
        DrawOutlinedTextNVG(vg,g_font_agency,infoBuf,{cx,92.0f*UI_SCALE()},24.0f*UI_SCALE(),nvgRGBA(255,255,255,255),nvgRGBA(0,0,0,255),true,2.0f);
    } else {
        DrawSoftTextNVG(vg,g_font_agency,"Asuka追踪 @Asuka1314",{cx,60.0f*UI_SCALE()},40.0f,nvgRGBA(255,255,255,255),true);
        char infoBuf[64]; snprintf(infoBuf,sizeof(infoBuf),"真人: %d  人机: %d",RealCount,BotCount);
        DrawSoftTextNVG(vg,g_font_agency,infoBuf,{cx,105.0f*UI_SCALE()},24.0f,nvgRGBA(255,255,255,255),true);
    }
    nvgEndFrame(vg);
}


// ==================== 触控优化样式（只生效一次）====================
static void ApplyMenuStyle() {
    static bool done = false;
    if (done) return;
    done = true;
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding  = 12.0f;
    st.FrameRounding   = 8.0f;
    st.GrabRounding    = 8.0f;
    st.ScrollbarRounding = 8.0f;
    st.FramePadding    = ImVec2(14, 10);   // 控件命中区加大
    st.ItemSpacing     = ImVec2(12, 14);   // 控件间距拉开
    st.ItemInnerSpacing= ImVec2(10, 6);
    st.GrabMinSize     = 24.0f;            // 滑块拖拽块更大
    st.ScrollbarSize   = 18.0f;
    st.WindowPadding   = ImVec2(14, 14);
}

// ==================== 分段大按钮（触发/部位选择）====================
static void Segmented(int* cur, const char* const labels[], int n) {
    float w = (ImGui::GetContentRegionAvail().x - (n - 1) * 8.0f) / n;
    for (int i = 0; i < n; i++) {
        if (i) ImGui::SameLine(0, 8);
        bool on = (*cur == i);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 1.0f, 1.0f));
        if (ImGui::Button(labels[i], ImVec2(w, 44))) *cur = i;
        if (on) ImGui::PopStyleColor();
    }
}

// ==================== 双列复选框 ====================
static void CB2(const char* label, bool* v) {
    ImGui::Checkbox(label, v);
}

// ==================== 主菜单 ====================
void Layout_tick_UI(bool* main_thread_flag) {
    UpdateGameData();
    ApplyMenuStyle();

    static int page = 0;   // 0=绘制 1=追锁 2=系统
    ImGui::SetNextWindowSize(ImVec2(640, 800), ImGuiCond_FirstUseEver);
    ImGui::Begin("Asuka追踪 ###AsukaMenu", main_thread_flag);

    // ===== 顶部状态条 =====
    ImGui::TextColored(ImVec4(0,1,1,1), "断点:%s  目标:%s  触发:%llu  速:%.0f",
        g_bpSet ? "是" : "否", g_Target.valid ? "有" : "无",
        (unsigned long long)g_bpHits, g_BulletSpeed);
    ImGui::Separator();

    // ===== 顶部翻页 =====
    {
        const char* const nav[] = { "绘 制", "追 锁", "系 统" };
        float w = (ImGui::GetContentRegionAvail().x - 16) / 3;
        for (int i = 0; i < 3; i++) {
            if (i) ImGui::SameLine(0, 8);
            bool on = (page == i);
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f,0.55f,1.0f,1.0f));
            if (ImGui::Button(nav[i], ImVec2(w, 50))) page = i;
            if (on) ImGui::PopStyleColor();
        }
    }
    ImGui::Spacing();

    ImGui::BeginChild("##content", ImVec2(0, 0), true);

    if (page == 0) {
        // ================= 绘制页 =================
        ImGui::TextDisabled("── 目标显示 ──");
        ImGui::Columns(2, "esp", false);
        CB2("方框",   &DrawIo[1]);
        CB2("骨骼",   &DrawIo[4]);
        CB2("射线",   &DrawIo[3]);
        CB2("信息",   &DrawIo[5]);
        CB2("血量",   &DrawIo[6]);
        CB2("距离",   &DrawIo[2]);
        ImGui::NextColumn();
        CB2("载具",   &DrawIo[8]);
        CB2("手持",   &DrawIo[9]);
        CB2("地址",   &DrawIo[7]);
        CB2("忽略人机", &忽略人机);
        CB2("标题描边", &g_titleOutline);
        CB2("FOV圈",  &g_fovCircle);
        ImGui::Columns(1);
    }
    else if (page == 1) {
        // ================= 追锁页 =================
        {
            bool on = g_Aim.enable;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f,0.2f,0.2f,1.0f));
            if (ImGui::Button(on ? "追锁:开" : "追锁:关", ImVec2(-1, 54))) g_Aim.enable = !on;
            if (on) ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("── 触发条件 ──");
        { const char* const t[] = { "总是", "开火", "开镜" }; Segmented(&g_Aim.trigger, t, 3); }
        ImGui::TextDisabled("── 瞄准部位 ──");
        { const char* const p[] = { "头部", "胸部", "盆骨" }; Segmented(&g_Aim.part, p, 3); }
        ImGui::Spacing();
        ImGui::TextDisabled("── 弹道 ──");
        ImGui::Checkbox("预判", &g_Aim.predict);
        if (g_Aim.predict) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(140);
            ImGui::SliderFloat("##pred", &g_Aim.predScale, 0.0f, 3.0f, "强度 %.2f");
        }
        ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("下坠系数", &g_Aim.dropCoef, 0.0f, 1200.0f, "%.0f");
        ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("压枪补偿", &g_Aim.recoilComp, 0.0f, 20.0f, "%.1f");
        ImGui::TextDisabled("── 范围 ──");
        ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("FOV", &g_Aim.fov, 50.0f, 800.0f, "%.0f");
        ImGui::Checkbox("动态圈", &g_Aim.dynFov);
        ImGui::Checkbox("概率模式", &g_Aim.prob);
        if (g_Aim.prob) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(140);
            ImGui::SliderFloat("##rate", &g_Aim.probRate, 0.05f, 1.0f, "%.2f");
        }
    }
    else {
        // ================= 系统页 =================
        static int style_idx = 0;
        ImGui::TextDisabled("── 主题 ──");
        { const char* const s[] = { "白", "蓝", "紫" }; Segmented(&style_idx, s, 3); }
        switch (style_idx) { case 0: ImGui::StyleColorsLight(); break; case 1: ImGui::StyleColorsDark(); break; case 2: ImGui::StyleColorsClassic(); break; }
        ImGui::Spacing();
        ImGui::Checkbox("过录制", &::permeate_record);
        if (::permeate_record) ::permeate_record_ini = true;
        ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("刷新帧率", &FPS, 60.0f, 144.0f, "%.0f");
        ImGui::Spacing();
        ImGui::TextDisabled("── 调试 ──");
        ImGui::BulletText("进程:%d", pid);
        ImGui::BulletText("矩阵:%lx", Matrix);
        ImGui::BulletText("自身:%lx", MySelf);
        ImGui::BulletText("世界:%lx", Arrayaddr);
        ImGui::BulletText("数量:%d", Count);
    }

    ImGui::EndChild();

    ImGui::TextColored(ImVec4(1,0,1,1), "%.1f FPS", ImGui::GetIO().Framerate);
    g_window = ImGui::GetCurrentWindow();
    ImGui::End();
}