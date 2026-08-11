// src/Android_draw/draw_Gui.cpp
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
static int RealCount = 0;
static int BotCount = 0;

struct BoneCache { long obj; int ok; Vector2A p[15]; };
static BoneCache g_boneCache[64];

// ==================== 测试追踪配置/状态 ====================
static struct {
    bool enable = false;
    int  mode = 0;        // 0角度追踪 1坐标追踪 2寄存器转储
    int  part = 0;        // 0头 1胸 2盆骨
    int  trigger = 0;     // 0总是 1开火 2开镜
    int  regLoc = 0;      // StartLoc: 0=Q0 1=Q1 2=X1指针
    int  regRot = 1;      // StartRot: 0=Q0 1=Q1 2=X2指针
    bool predict = true;
    bool drop = true;
    bool prob = false;
    float probRate = 0.8f;
    float fov = 250.0f;
} g_Aim;

static struct {
    bool valid = false;
    Vector3A pos;
    Vector3A vel;
    float screenDist = 0;
} g_Target;

static float g_BulletSpeed = 600.0f;
static volatile uint64_t g_bpHits = 0;
static volatile bool g_bpSet = false;

static struct {
    uint64_t x0, x1, x2, x3;
    float q0[4], q1[4], q2[4], q3[4];
    uint64_t hits;
} g_Dump;

static Vector3A UnpackQ(__uint128_t q) { float* f = (float*)&q; Vector3A v; v.X=f[0]; v.Y=f[1]; v.Z=f[2]; return v; }
static void PackQ(__uint128_t& q, float a, float b, float c) { float* f = (float*)&q; f[0]=a; f[1]=b; f[2]=c; }

static void AimFrameBegin() { g_Target.valid = false; g_Target.screenDist = 1e9f; }
static void AimFeedTarget(const Vector3A& pos, const Vector3A& vel, float sd) {
    if (sd < g_Target.screenDist) { g_Target.valid = true; g_Target.pos = pos; g_Target.vel = vel; g_Target.screenDist = sd; }
}
static void AimFeedBulletSpeed(float v) { if (v > 50.0f) g_BulletSpeed = v; }

// ==================== 追踪线程 ====================
static void* HwbpAimThread(void*) {
    while (true) {
        if (!初始化 || !libbase || !MySelf) { usleep(50000); continue; }
        if (!g_Aim.enable) {
            if (g_bpSet) { driver->hwbp_remove(); g_bpSet = false; }
            usleep(50000); continue;
        }
        if (!g_bpSet) {
            paradise_hwbp_point_config pt; memset(&pt, 0, sizeof(pt));
            pt.address = (uint64_t)libbase + 0x6DFE100;
            pt.type = PARADISE_HWBP_EXECUTE;
            pt.length = 4;
            pt.scope = PARADISE_HWBP_ALL_THREADS;
            g_bpSet = driver->hwbp_set(&pt, 1);
            if (!g_bpSet) { usleep(100000); continue; }
        }
        bool trigOk = true;
        if (g_Aim.trigger == 1)      trigOk = driver->read<int>(MySelf + 0x1830) != 0;
        else if (g_Aim.trigger == 2) trigOk = driver->read<int>(MySelf + 0x1134) != 0;

        paradise_hwbp_record recs[8];
        uint32_t cnt = 0;
        if (driver->hwbp_get_records(0, 0, recs, 8, &cnt) && cnt > 0) {
            for (uint32_t i = 0; i < cnt; i++) {
                g_bpHits++;
                auto& rec = recs[i];

                if (g_Aim.mode == 2) {
                    g_Dump.x0=rec.x0; g_Dump.x1=rec.x1; g_Dump.x2=rec.x2; g_Dump.x3=rec.x3;
                    memcpy(g_Dump.q0,&rec.q0,16); memcpy(g_Dump.q1,&rec.q1,16);
                    memcpy(g_Dump.q2,&rec.q2,16); memcpy(g_Dump.q3,&rec.q3,16);
                    g_Dump.hits = g_bpHits;
                    printf("[HWBP]#%llu X0=%llx X1=%llx X2=%llx X3=%llx Q0=(%.1f,%.1f,%.1f) Q1=(%.1f,%.1f,%.1f)\n",
                        (unsigned long long)g_bpHits, (unsigned long long)rec.x0, (unsigned long long)rec.x1,
                        (unsigned long long)rec.x2, (unsigned long long)rec.x3,
                        g_Dump.q0[0], g_Dump.q0[1], g_Dump.q0[2], g_Dump.q1[0], g_Dump.q1[1], g_Dump.q1[2]);
                    continue;
                }

                if (!trigOk || !g_Target.valid) continue;
                if (g_Aim.prob && ((float)rand()/RAND_MAX) > g_Aim.probRate) continue;

                Vector3A start;
                if (g_Aim.regLoc == 0)      start = UnpackQ(rec.q0);
                else if (g_Aim.regLoc == 1) start = UnpackQ(rec.q1);
                else driver->read((uintptr_t)rec.x1, &start, sizeof(start));
                if (fabsf(start.X) < 1.0f && fabsf(start.Y) < 1.0f && fabsf(start.Z) < 1.0f) continue;

                Vector3A aim = g_Target.pos;
                float dist = sqrtf(powf(aim.X-start.X,2)+powf(aim.Y-start.Y,2)+powf(aim.Z-start.Z,2));
                float t = dist / g_BulletSpeed;
                if (g_Aim.predict) { aim.X += g_Target.vel.X*t; aim.Y += g_Target.vel.Y*t; aim.Z += g_Target.vel.Z*t; }
                if (g_Aim.drop)    { aim.Z += 500.0f * t * t; }

                if (g_Aim.mode == 1) {
                    if (g_Aim.regLoc == 0)      { PackQ(rec.q0, aim.X, aim.Y, aim.Z); PARADISE_BP_SET_MASK(&rec, PARADISE_REG_Q0, PARADISE_BP_OP_WRITE); }
                    else if (g_Aim.regLoc == 1) { PackQ(rec.q1, aim.X, aim.Y, aim.Z); PARADISE_BP_SET_MASK(&rec, PARADISE_REG_Q1, PARADISE_BP_OP_WRITE); }
                    else driver->write((uintptr_t)rec.x1, &aim, sizeof(aim));
                }

                float dx = aim.X-start.X, dy = aim.Y-start.Y, dz = aim.Z-start.Z;
                float hyp = sqrtf(dx*dx + dy*dy);
                float pitch = atan2f(dz, hyp) * (180.0f/3.14159265f);
                float yaw   = atan2f(dy, dx)  * (180.0f/3.14159265f);

                if (g_Aim.regRot == 0)      { PackQ(rec.q0, pitch, yaw, 0); PARADISE_BP_SET_MASK(&rec, PARADISE_REG_Q0, PARADISE_BP_OP_WRITE); }
                else if (g_Aim.regRot == 1) { PackQ(rec.q1, pitch, yaw, 0); PARADISE_BP_SET_MASK(&rec, PARADISE_REG_Q1, PARADISE_BP_OP_WRITE); }
                else { float rot[3] = {pitch, yaw, 0}; driver->write((uintptr_t)rec.x2, rot, sizeof(rot)); }

                driver->hwbp_set_record(0, i, &rec);
            }
            driver->hwbp_clear_records(0);
        }
        usleep(500);
    }
    return nullptr;
}

void DrawInit() {
    if (初始化) return;
    pid = getPID("com.rekoo.pubgm");
    if (pid <= 0) { printf("游戏未启动\n"); return; }
    libbase = getModuleBase("libUE4.so");
    if (libbase <= 0) { printf("libUE4.so 未找到\n"); return; }
    初始化 = true;
    printf("初始化成功! libUE4: %lx, pid: %d\n", libbase, pid);
    pthread_t t;
    pthread_create(&t, nullptr, HwbpAimThread, nullptr);
    pthread_detach(t);
}

void UpdateGameData() {
    if (!初始化) return;
    Matrix = driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1d5f70) + 0xC0) + 0x590;
    Uworld = driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1fb900) + 0x810) + 0x78);
    Uleve = driver->read<uint64_t>(Uworld + 0x30);
    Arrayaddr = driver->read<uint64_t>(Uleve + 0xA0);
    Count = driver->read<int>(Uleve + 0xA8);
    MySelf = driver->read<uint64_t>(
        driver->read<uint64_t>(
            driver->read<uint64_t>(
                driver->read<uint64_t>(
                    driver->read<uint64_t>(
                        driver->read<uint64_t>(
                            driver->read<uint64_t>(libbase + 0xf1fb900) + 0x810
                        ) + 0x78
                    ) + 0x38
                ) + 0x78
            ) + 0x30
        ) + 0x28c8
    );
    类地址 = driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xec73720) + 0x110);
    memset(matrix, 0, 16);
    driver->read((uintptr_t)Matrix, matrix, 16 * 4);

    long wq1 = driver->read<uint64_t>(MySelf + 0x2608);
    if (wq1 > 0x10000000 && wq1 < 0x10000000000) {
        long wq2 = driver->read<uint64_t>(wq1 + 0x5D8);
        if (wq2 > 0x10000000 && wq2 < 0x10000000000) {
            float bs = driver->read<float>(wq2 + 0x560);
            if (bs > 10000) bs *= 0.01f;
            AimFeedBulletSpeed(bs);
        }
    }
}

static void SetFontNVG(NVGcontext* vg, int fontId) {
    nvgFontFaceId(vg, fontId);
    if (fontId == g_font_agency && g_nvg_font >= 0) nvgAddFallbackFont(vg, "agency", "zh");
}
static float UI_SCALE() { return (float)abs_ScreenY / 1080.0f; }
static void TextDrawScaled(NVGcontext* vg, float x, float y, float sx, const char* text) {
    nvgSave(vg); nvgTranslate(vg, x, y); nvgScale(vg, sx, 1.0f); nvgText(vg, 0, 0, text, NULL); nvgRestore(vg);
}
static void DrawSoftTextNVG(NVGcontext* vg, int fontId, const char* text, Vector2A pos, float fontSize, NVGcolor color, bool isCenter = true) {
    if (!vg || !text || fontId < 0) return;
    float fs = fontSize * UI_SCALE();
    nvgFontSize(vg, fs); SetFontNVG(vg, fontId);
    nvgTextLetterSpacing(vg, fs * 0.02f);
    nvgTextAlign(vg, (isCenter ? NVG_ALIGN_CENTER : NVG_ALIGN_LEFT) | NVG_ALIGN_MIDDLE);
    float y = pos.Y + fs * 0.06f;
    nvgFontBlur(vg, 4.0f); nvgFillColor(vg, nvgRGBA(0,0,0,180));
    TextDrawScaled(vg, pos.X, y + fs*0.06f, 1.06f, text);
    nvgFontBlur(vg, 0.0f);
    float o = fs * 0.04f + 0.6f;
    nvgFillColor(vg, nvgRGBA(0,0,0,255));
    TextDrawScaled(vg, pos.X-o, y-o, 1.06f, text); TextDrawScaled(vg, pos.X+o, y-o, 1.06f, text);
    TextDrawScaled(vg, pos.X-o, y+o, 1.06f, text); TextDrawScaled(vg, pos.X+o, y+o, 1.06f, text);
    nvgFillColor(vg, color);
    TextDrawScaled(vg, pos.X, y, 1.06f, text);
    nvgTextLetterSpacing(vg, 0.0f);
}
void DrawOutlinedTextNVG(NVGcontext* vg, int fontId, const char* text, Vector2A pos, float fontSize,
                         NVGcolor color, NVGcolor outlineColor, bool isCenter = false, float outlineWidth = 1.0f) {
    if (!vg || !text || fontId < 0) return;
    nvgFontSize(vg, fontSize); SetFontNVG(vg, fontId);
    nvgTextAlign(vg, (isCenter ? NVG_ALIGN_CENTER : NVG_ALIGN_LEFT) | NVG_ALIGN_TOP);
    nvgFillColor(vg, outlineColor);
    const float o = outlineWidth;
    nvgText(vg, pos.X-o, pos.Y-o, text, NULL); nvgText(vg, pos.X+o, pos.Y-o, text, NULL);
    nvgText(vg, pos.X-o, pos.Y+o, text, NULL); nvgText(vg, pos.X+o, pos.Y+o, text, NULL);
    nvgFillColor(vg, color);
    nvgText(vg, pos.X, pos.Y, text, NULL);
}
static void DrawLineNVG(NVGcontext* vg, float x1, float y1, float x2, float y2, NVGcolor color, float thickness = 1.5f) {
    if (!vg) return;
    nvgBeginPath(vg); nvgMoveTo(vg, x1, y1); nvgLineTo(vg, x2, y2);
    nvgStrokeColor(vg, color); nvgStrokeWidth(vg, thickness); nvgStroke(vg);
}
static void DrawHexagonStarNVG(NVGcontext* vg, float x, float y, float size, float rotation, float thickness = 1.5f) {
    if (!vg) return;
    NVGcolor white = nvgRGBA(255,255,255,255), black = nvgRGBA(0,0,0,255);
    float ptx[6], pty[6];
    for (int i = 0; i < 6; i++) { float a = rotation + 2.0f*NVG_PI*i/6.0f; ptx[i]=x+size*cosf(a); pty[i]=y+size*sinf(a); }
    float bt = thickness + 2.0f;
    DrawLineNVG(vg,ptx[0],pty[0],ptx[2],pty[2],black,bt); DrawLineNVG(vg,ptx[2],pty[2],ptx[4],pty[4],black,bt);
    DrawLineNVG(vg,ptx[4],pty[4],ptx[0],pty[0],black,bt); DrawLineNVG(vg,ptx[1],pty[1],ptx[3],pty[3],black,bt);
    DrawLineNVG(vg,ptx[3],pty[3],ptx[5],pty[5],black,bt); DrawLineNVG(vg,ptx[5],pty[5],ptx[1],pty[1],black,bt);
    DrawLineNVG(vg,ptx[0],pty[0],ptx[2],pty[2],white,thickness); DrawLineNVG(vg,ptx[2],pty[2],ptx[4],pty[4],white,thickness);
    DrawLineNVG(vg,ptx[4],pty[4],ptx[0],pty[0],white,thickness); DrawLineNVG(vg,ptx[1],pty[1],ptx[3],pty[3],white,thickness);
    DrawLineNVG(vg,ptx[3],pty[3],ptx[5],pty[5],white,thickness); DrawLineNVG(vg,ptx[5],pty[5],ptx[1],pty[1],white,thickness);
}
static void DrawLogoNVG(NVGcontext* vg, float x, float y, float size) {
    static float rotation = 0.0f; rotation += 0.05f;
    DrawHexagonStarNVG(vg, x, y, size, rotation, 1.5f);
}

void DrawPlayerNVG(NVGcontext* vg) {
    if (!初始化 || MySelf == 0 || vg == nullptr) return;
    auto onScreen = [&](const Vector2A& p) { return p.X>0 && p.Y>0 && p.X<(float)abs_ScreenX && p.Y<(float)abs_ScreenY; };

    int 自己队伍 = driver->read<int>(MySelf + 0x998);
    Vector3A Z;
    driver->read((uintptr_t)(driver->read<uint64_t>(MySelf + 0x208) + 0x1c8), &Z, sizeof(Z));

    PlayerCount = 0; RealCount = 0; BotCount = 0;
    AimFrameBegin();

    for (int i = 0; i < Count; i++) {
        long int Objaddr = driver->read<uint64_t>(Arrayaddr + 0x8 * i);
        if (Objaddr <= 0xffff || Objaddr == 0 || Objaddr <= 0x10000000 || Objaddr % 4 != 0 || Objaddr >= 0x10000000000) continue;
        if (MySelf == Objaddr) continue;

        int ClassID = driver->read<int>(Objaddr + 24);
        long FNameEntry = driver->read<uint64_t>(driver->read<uint64_t>(类地址 + (ClassID/0x4000)*0x8) + (ClassID%0x4000)*0x8);
        char ClassName[64] = "";
        driver->read((uintptr_t)(FNameEntry + 0xC), ClassName, 64);
        if (strstr(ClassName, "BPPawn_Escape_Raven") != 0 || strstr(ClassName, "BPPawn_Escape_UAV_C") != 0) continue;

        if (DrawIo[8]) {
            const char* vname = NULL;
            if (strstr(ClassName,"UAZ")) vname="吉普车"; else if (strstr(ClassName,"Dacia")) vname="轿车";
            else if (strstr(ClassName,"Buggy")) vname="蹦蹦车"; else if (strstr(ClassName,"Mirado")) vname="跑车";
            else if (strstr(ClassName,"Rony")) vname="越野皮卡"; else if (strstr(ClassName,"PickUp")) vname="皮卡车";
            else if (strstr(ClassName,"MiniBus")) vname="迷你巴士"; else if (strstr(ClassName,"PG117")) vname="快艇";
            else if (strstr(ClassName,"AquaRail")) vname="摩托艇"; else if (strstr(ClassName,"VH_Motorcycle")) vname="摩托车";
            else if (strstr(ClassName,"VH_Snowmobile")||strstr(ClassName,"VH_Snowbike")) vname="雪地摩托";
            else if (strstr(ClassName,"VH_Scooter")||strstr(ClassName,"Scooter")) vname="踏板车";
            else if (strstr(ClassName,"VH_ATV1")) vname="地形车"; else if (strstr(ClassName,"VH_UTV")) vname="UTV";
            else if (strstr(ClassName,"VH_BRDM")) vname="装甲车"; else if (strstr(ClassName,"VH_4SportCar")) vname="敞篷跑车";
            else if (strstr(ClassName,"_CoupeRB_")) vname="双人跑车"; else if (strstr(ClassName,"Bigfoot")) vname="大脚车";
            else if (strstr(ClassName,"TrackVehicle")) vname="地铁矿车"; else if (strstr(ClassName,"BP_VH_Tuk_")) vname="三轮摩托";
            else if (strstr(ClassName,"LadaNiva")) vname="雪地越野车"; else if (strstr(ClassName,"AirDropPlane")) vname="空投飞机";
            else if (strstr(ClassName,"VH_Motorglider")||strstr(ClassName,"wing_")) vname="飞行器";
            else if (strstr(ClassName,"Horse")) vname="马"; else if (strstr(ClassName,"Bike")) vname="自行车";
            if (vname) {
                long int vptr = driver->read<uint64_t>(Objaddr + 0x208);
                if (vptr > 0x10000000 && vptr < 0x10000000000) {
                    Vector3A V; driver->read((uintptr_t)(vptr + 0x1c8), &V, sizeof(V));
                    float vc = matrix[3]*V.X + matrix[7]*V.Y + matrix[11]*V.Z + matrix[15];
                    if (vc > 0.001f) {
                        float vDist = sqrt(pow(V.X-Z.X,2)+pow(V.Y-Z.Y,2)+pow(V.Z-Z.Z,2)) * 0.01f;
                        if (vDist <= 400) {
                            float vx = px + (matrix[0]*V.X+matrix[4]*V.Y+matrix[8]*V.Z+matrix[12])/vc*px;
                            float vy = py - (matrix[1]*V.X+matrix[5]*V.Y+matrix[9]*V.Z+matrix[13])/vc*py;
                            if (vx>0 && vx<(float)abs_ScreenX && vy>0 && vy<(float)abs_ScreenY) {
                                char vbuf[64]; snprintf(vbuf, sizeof(vbuf), "%s [%dm]", vname, (int)vDist);
                                DrawOutlinedTextNVG(vg, g_font_agency, vbuf, {vx, vy-20}, 20.0f,
                                    nvgRGBA(255,255,0,255), nvgRGBA(0,0,0,255), true, 1.0f);
                            }
                        }
                    }
                }
                continue;
            }
        }

        float 玩家标志 = driver->read<float>(Objaddr + 0x2b78);
        bool isDog = (strstr(ClassName, "AIMob_PatrolDog_C") != 0);
        bool isHunger = (strstr(ClassName, "BPPawn_HungerH_C") != 0) || (strstr(ClassName, "BPPawn_HungerB_C") != 0);
        if (玩家标志 != 479.5f && !isDog && !isHunger) continue;

        int 状态 = driver->read<int>(Objaddr + 0x1058);
        if (状态 == 1048592 || 状态 == 1048576) continue;

        int 敌人队伍 = driver->read<int>(Objaddr + 0x998);
        if (敌人队伍 == 自己队伍) continue;

        int botFlag = driver->read<int>(Objaddr + 0xa59);
        bool isBot = isDog || isHunger || (敌人队伍 != 0 && (botFlag==16842753 || botFlag==16843009 || botFlag==16843008));
        if (忽略人机 && isBot) continue;

        float 当前血量 = driver->read<float>(Objaddr + 0xe60);
        float 最大血量 = driver->read<float>(Objaddr + 0xe64);
        if (最大血量 <= 0) continue;

        PlayerCount++; if (isBot) BotCount++; else RealCount++;

        Vector3A D;
        driver->read((uintptr_t)(driver->read<uint64_t>(Objaddr + 0x208) + 0x1c8), &D, sizeof(D));
        float camera = matrix[3]*D.X + matrix[7]*D.Y + matrix[11]*D.Z + matrix[15];
        if (camera <= 0.001f) continue;
        float Distance = sqrt(pow(D.X-Z.X,2)+pow(D.Y-Z.Y,2)+pow(D.Z-Z.Z,2)) * 0.01f;
        if (Distance > 500 || Distance <= 0) continue;

        float r_x = px + (matrix[0]*D.X+matrix[4]*D.Y+matrix[8]*D.Z+matrix[12])/camera*px;
        float r_y = py - (matrix[1]*D.X+matrix[5]*D.Y+matrix[9]*(D.Z-5)+matrix[13])/camera*py;
        float r_w = py - (matrix[1]*D.X+matrix[5]*D.Y+matrix[9]*(D.Z+205)+matrix[13])/camera*py;
        float W = (r_y - r_w) / 2;
        if (W <= 0) continue;
        float MIDDLE = r_x, TOP_FALLBACK = r_y - W, BOTTOM_FALLBACK = r_y + W;

        Vector2A Head, Chest, Pelvis, Left_Shoulder, Right_Shoulder, Left_Elbow, Right_Elbow,
                 Left_Wrist, Right_Wrist, Left_Thigh, Right_Thigh, Left_Knee, Right_Knee, Left_Ankle, Right_Ankle;
        bool bonesOk = false;
        Vector3A wHeadW, wChestW, wPelvisW;

        if (DrawIo[4]) {
            long int Mesh = driver->read<uint64_t>(Objaddr + 0x510);
            if (Mesh > 0x10000000 && Mesh < 0x10000000000) {
                long int boneArrayPtr = driver->read<uint64_t>(Mesh + 0x9a8);
                int BoneCount = driver->read<int>(Mesh + 0x9a8 + 8);
                if (boneArrayPtr > 0x10000000 && boneArrayPtr < 0x10000000000 && BoneCount > 0 && BoneCount < 200) {
                    int idx_head=5, idx_chest=4, idx_pelvis=0;
                    int idx_lsh, idx_rsh, idx_lelb, idx_relb, idx_lw, idx_rw, idx_lth, idx_rth, idx_lk, idx_rk, idx_la, idx_ra;
                    if (isDog) { idx_lsh=7; idx_rsh=11; idx_lelb=8; idx_relb=12; idx_lw=9; idx_rw=13; idx_lth=14; idx_rth=18; idx_lk=15; idx_rk=19; idx_la=16; idx_ra=20; }
                    else if (isHunger) { idx_lsh=11; idx_rsh=18; idx_lelb=12; idx_relb=19; idx_lw=13; idx_rw=20; idx_lth=24; idx_rth=29; idx_lk=25; idx_rk=30; idx_la=26; idx_ra=31; }
                    else if (BoneCount == 67) { idx_lsh=13; idx_rsh=34; idx_lelb=14; idx_relb=35; idx_lw=16; idx_rw=37; idx_lth=54; idx_rth=58; idx_lk=55; idx_rk=59; idx_la=56; idx_ra=60; }
                    else if (BoneCount == 29) { idx_lsh=7; idx_rsh=13; idx_lelb=8; idx_relb=14; idx_lw=9; idx_rw=15; idx_lth=18; idx_rth=21; idx_lk=19; idx_rk=22; idx_la=20; idx_ra=23; }
                    else { idx_lsh=11; idx_rsh=32; idx_lelb=12; idx_relb=33; idx_lw=63; idx_rw=62; idx_lth=52; idx_rth=56; idx_lk=53; idx_rk=57; idx_la=54; idx_ra=58; }

                    long int human = Mesh + 0x210;
                    long int Bone = boneArrayPtr + 0x30;
                    FTransform meshtrans = getBone(human);
                    FMatrix c2wMatrix = TransformToMatrix(meshtrans);
                    auto BW = [&](int idx) { return MarixToVector(MatrixMulti(TransformToMatrix(getBone(Bone + idx*48)), c2wMatrix)); };

                    wHeadW = BW(idx_head); wHeadW.Z += 7.0f;
                    wChestW = BW(idx_chest); wPelvisW = BW(idx_pelvis);
                    Vector3A wLSh=BW(idx_lsh), wRSh=BW(idx_rsh), wLElb=BW(idx_lelb), wRElb=BW(idx_relb);
                    Vector3A wLW=BW(idx_lw), wRW=BW(idx_rw), wLTh=BW(idx_lth), wRTh=BW(idx_rth);
                    Vector3A wLK=BW(idx_lk), wRK=BW(idx_rk), wLA=BW(idx_la), wRA=BW(idx_ra);

                    auto okw = [&](const Vector3A& p) { return fabsf(p.X-wPelvisW.X)<300 && fabsf(p.Y-wPelvisW.Y)<300 && fabsf(p.Z-wPelvisW.Z)<300; };

                    if (okw(wHeadW)&&okw(wChestW)&&okw(wLSh)&&okw(wRSh)&&okw(wLElb)&&okw(wRElb)&&okw(wLW)&&okw(wRW)&&
                        okw(wLTh)&&okw(wRTh)&&okw(wLK)&&okw(wRK)&&okw(wLA)&&okw(wRA)) {
                        Head=WorldToScreen(wHeadW,matrix,camera); Chest=WorldToScreen(wChestW,matrix,camera);
                        Pelvis=WorldToScreen(wPelvisW,matrix,camera);
                        Left_Shoulder=WorldToScreen(wLSh,matrix,camera); Right_Shoulder=WorldToScreen(wRSh,matrix,camera);
                        Left_Elbow=WorldToScreen(wLElb,matrix,camera); Right_Elbow=WorldToScreen(wRElb,matrix,camera);
                        Left_Wrist=WorldToScreen(wLW,matrix,camera); Right_Wrist=WorldToScreen(wRW,matrix,camera);
                        Left_Thigh=WorldToScreen(wLTh,matrix,camera); Right_Thigh=WorldToScreen(wRTh,matrix,camera);
                        Left_Knee=WorldToScreen(wLK,matrix,camera); Right_Knee=WorldToScreen(wRK,matrix,camera);
                        Left_Ankle=WorldToScreen(wLA,matrix,camera); Right_Ankle=WorldToScreen(wRA,matrix,camera);

                        bonesOk = onScreen(Head)&&onScreen(Chest)&&onScreen(Pelvis)&&onScreen(Left_Shoulder)&&onScreen(Right_Shoulder)&&
                                  onScreen(Left_Elbow)&&onScreen(Right_Elbow)&&onScreen(Left_Wrist)&&onScreen(Right_Wrist)&&
                                  onScreen(Left_Thigh)&&onScreen(Right_Thigh)&&onScreen(Left_Knee)&&onScreen(Right_Knee)&&
                                  onScreen(Left_Ankle)&&onScreen(Right_Ankle);

                        BoneCache& bc = g_boneCache[(int)((Objaddr>>4)&63)];
                        if (bonesOk) {
                            bc.obj=Objaddr; bc.ok=1;
                            bc.p[0]=Head; bc.p[1]=Chest; bc.p[2]=Pelvis; bc.p[3]=Left_Shoulder; bc.p[4]=Right_Shoulder;
                            bc.p[5]=Left_Elbow; bc.p[6]=Right_Elbow; bc.p[7]=Left_Wrist; bc.p[8]=Right_Wrist;
                            bc.p[9]=Left_Thigh; bc.p[10]=Right_Thigh; bc.p[11]=Left_Knee; bc.p[12]=Right_Knee;
                            bc.p[13]=Left_Ankle; bc.p[14]=Right_Ankle;

                            // 喂追锁目标
                            Vector2A partSc = (g_Aim.part==0)?Head:((g_Aim.part==1)?Chest:Pelvis);
                            Vector3A partW = (g_Aim.part==0)?wHeadW:((g_Aim.part==1)?wChestW:wPelvisW);
                            float sd = sqrtf(powf(partSc.X-px,2)+powf(partSc.Y-py,2));
                            if (sd < g_Aim.fov) {
                                Vector3A vel; memset(&vel,0,sizeof(vel));
                                long vp = driver->read<uint64_t>(Objaddr + 0x208);
                                if (vp > 0x10000000 && vp < 0x10000000000) driver->read(vp + 0x2C0, &vel, sizeof(vel));
                                AimFeedTarget(partW, vel, sd);
                            }
                        }
                    }
                    if (!bonesOk) {
                        BoneCache& bc = g_boneCache[(int)((Objaddr>>4)&63)];
                        if (bc.obj==Objaddr && bc.ok) {
                            Head=bc.p[0]; Chest=bc.p[1]; Pelvis=bc.p[2]; Left_Shoulder=bc.p[3]; Right_Shoulder=bc.p[4];
                            Left_Elbow=bc.p[5]; Right_Elbow=bc.p[6]; Left_Wrist=bc.p[7]; Right_Wrist=bc.p[8];
                            Left_Thigh=bc.p[9]; Right_Thigh=bc.p[10]; Left_Knee=bc.p[11]; Right_Knee=bc.p[12];
                            Left_Ankle=bc.p[13]; Right_Ankle=bc.p[14];
                            bonesOk = true;
                        }
                    }
                }
            }
        }

        float headX = (bonesOk&&Head.X>0)?Head.X:MIDDLE;
        float headY = (bonesOk&&Head.Y>0)?Head.Y:TOP_FALLBACK;
        float left = headX - W*0.6f, right = headX + W*0.6f;
        float top = (bonesOk&&Head.Y>0)?(Head.Y - W/5.0f):TOP_FALLBACK;
        float bottom = (bonesOk)?((Left_Ankle.Y<Right_Ankle.Y)?Right_Ankle.Y+W/10.0f:Left_Ankle.Y+W/10.0f):BOTTOM_FALLBACK;

        NVGcolor COL_WHITE = nvgRGBA(255,255,255,255), COL_BLACK = nvgRGBA(0,0,0,255);

        if (DrawIo[1]) {
            DrawLineNVG(vg,left,top,right,top,COL_WHITE,1.5f); DrawLineNVG(vg,right,top,right,bottom,COL_WHITE,1.5f);
            DrawLineNVG(vg,right,bottom,left,bottom,COL_WHITE,1.5f); DrawLineNVG(vg,left,bottom,left,top,COL_WHITE,1.5f);
        }
        if (DrawIo[3]) DrawLineNVG(vg,(float)abs_ScreenX*0.5f,73.0f,headX,top,COL_WHITE,1.0f);
        if (DrawIo[4] && bonesOk) {
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
        if (DrawIo[2]) {
            char distBuf[16]; snprintf(distBuf, sizeof(distBuf), "%d m", (int)Distance);
            float yPos = bottom + 6;
            if (yPos > (float)abs_ScreenY - 25) yPos = (float)abs_ScreenY - 25;
            DrawOutlinedTextNVG(vg, g_font_agency, distBuf, {headX, yPos}, 25.0f, COL_WHITE, COL_BLACK, true, 1.0f);
        }
        if (DrawIo[5]) {
            char tagBuf[96];
            if (isBot) snprintf(tagBuf, sizeof(tagBuf), "[%d] 人机", 敌人队伍);
            else { getUTF8(PlayerName, driver->read<uint64_t>(Objaddr + 0x960)); snprintf(tagBuf, sizeof(tagBuf), "[%d] %s", 敌人队伍, PlayerName); }
            float yPos = top - 24; if (yPos < 0) yPos = 0;
            DrawOutlinedTextNVG(vg, g_font_agency, tagBuf, {headX, yPos}, 18.0f, COL_WHITE, COL_BLACK, true, 1.0f);
        }
        if (DrawIo[9]) {
            long int wq1 = driver->read<uint64_t>(Objaddr + 0x2608);
            if (wq1 > 0x10000000 && wq1 < 0x10000000000) {
                long int wq2 = driver->read<uint64_t>(wq1 + 0x5D8);
                if (wq2 > 0x10000000 && wq2 < 0x10000000000) {
                    int wID = driver->read<int>(wq2 + 24);
                    long wEntry = driver->read<uint64_t>(driver->read<uint64_t>(类地址 + (wID/0x4000)*0x8) + (wID%0x4000)*0x8);
                    char wName[64] = "";
                    driver->read((uintptr_t)(wEntry + 0xC), wName, 64);
                    char* p = strstr(wName, "Weap");
                    if (p) {
                        char clean[48]; strncpy(clean, p+4, sizeof(clean)-1); clean[sizeof(clean)-1]=0;
                        char* us = strstr(clean, "_"); if (us) *us = 0;
                        if (clean[0]) {
                            float wy = top - 44; if (wy < 0) wy = 0;
                            DrawOutlinedTextNVG(vg, g_font_agency, clean, {headX, wy}, 16.0f, COL_WHITE, COL_BLACK, true, 1.0f);
                        }
                    }
                }
            }
        }
        if (DrawIo[7]) {
            char buf[32]; sprintf(buf, "0x%lx", Objaddr);
            DrawOutlinedTextNVG(vg, g_font_agency, buf, {headX, bottom}, 16.0f, COL_WHITE, COL_BLACK, true, 1.0f);
        }
        if (DrawIo[6]) {
            float CurHP = 当前血量, MaxHP = 最大血量;
            if (MaxHP <= 0) MaxHP = 100.0f;
            if (CurHP < 0) CurHP = 0; if (CurHP > MaxHP) CurHP = MaxHP;
            float hp_ratio = CurHP / MaxHP;
            int hp_percent = (int)(hp_ratio * 100);
            float cx2 = headX, cy2 = top - 56.0f;
            nvgBeginPath(vg);
            nvgArc(vg, cx2, cy2, 24.0f, -NVG_PI/2.0f, -NVG_PI/2.0f + 2.0f*NVG_PI*hp_ratio, NVG_CW);
            nvgStrokeColor(vg, COL_WHITE); nvgStrokeWidth(vg, 4.0f); nvgLineCap(vg, NVG_ROUND); nvgStroke(vg);
            char hpBuf[8]; snprintf(hpBuf, sizeof(hpBuf), "%d%%", hp_percent);
            DrawOutlinedTextNVG(vg, g_font_agency, hpBuf, {cx2, cy2-6.0f}, 13.0f, COL_WHITE, COL_BLACK, true, 1.0f);
        }
    }
}

void DrawCanvas() {
    if (!vg) return;
    nvgBeginFrame(vg, (float)abs_ScreenX, (float)abs_ScreenY, 1.0f);
    float cx = (float)abs_ScreenX * 0.5f;
    DrawPlayerNVG(vg);
    DrawLogoNVG(vg, (float)abs_ScreenX/4.0f, (float)abs_ScreenY/10.0f, 35.0f);
    DrawSoftTextNVG(vg, g_font_agency, "Asuka追锁 @Asuka1314", {cx, 60.0f*UI_SCALE()}, 40.0f, nvgRGBA(255,255,255,255), true);
    char infoBuf[64]; snprintf(infoBuf, sizeof(infoBuf), "真人: %d  人机: %d", RealCount, BotCount);
    DrawSoftTextNVG(vg, g_font_agency, infoBuf, {cx, 105.0f*UI_SCALE()}, 24.0f, nvgRGBA(255,255,255,255), true);
    nvgEndFrame(vg);
}

void Layout_tick_UI(bool* main_thread_flag) {
    UpdateGameData();
    {
        static int style_idx = 0;
        ImGui::Begin("ImGui-UE4", main_thread_flag);
        if (::permeate_record_ini) {
            ImGui::SetWindowPos({LastCoordinate.Pos_x, LastCoordinate.Pos_y});
            ImGui::SetWindowSize({LastCoordinate.Size_x, LastCoordinate.Size_y});
            permeate_record_ini = false;
        }
        ImGui::Text("渲染模式 : %s, gui版本 : %s", graphics->RenderName, IMGUI_VERSION);
        if (ImGui::Combo("##主题", &style_idx, "白色主题\0蓝色主题\0紫色主题\0")) {
            switch (style_idx) { case 0: ImGui::StyleColorsLight(); break; case 1: ImGui::StyleColorsDark(); break; case 2: ImGui::StyleColorsClassic(); break; }
        }
        if (ImGui::Checkbox("过录制", &::permeate_record)) ::permeate_record_ini = true;
        if (ImGui::Button("初始化绘制", ImVec2(ImGui::GetContentRegionAvail().x, 50))) DrawInit();
        ImGui::ItemSize(ImVec2(0, 5));

        if (ImGui::BeginTabBar("MainTab")) {
            if (ImGui::BeginTabItem("绘制")) {
                ImGui::Checkbox("显示方框", &DrawIo[1]); ImGui::SameLine(0,40);
                ImGui::Checkbox("显示距离", &DrawIo[2]); ImGui::SameLine(0,40);
                ImGui::Checkbox("显示射线", &DrawIo[3]);
                ImGui::Checkbox("显示骨骼", &DrawIo[4]); ImGui::SameLine(0,40);
                ImGui::Checkbox("显示信息", &DrawIo[5]); ImGui::SameLine(0,40);
                ImGui::Checkbox("显示血量", &DrawIo[6]);
                ImGui::Checkbox("敌人地址", &DrawIo[7]); ImGui::SameLine(0,40);
                ImGui::Checkbox("忽略人机", &忽略人机);
                ImGui::Checkbox("显示载具", &DrawIo[8]); ImGui::SameLine(0,40);
                ImGui::Checkbox("显示手持", &DrawIo[9]);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("追锁")) {
                ImGui::Checkbox("启用断点追锁", &g_Aim.enable);
                ImGui::SameLine(0,20);
                ImGui::Combo("模式", &g_Aim.mode, "角度追踪\0坐标追踪\0寄存器转储\0");
                ImGui::Combo("部位", &g_Aim.part, "头部\0胸部\0盆骨\0");
                ImGui::SameLine(0,20);
                ImGui::Combo("触发", &g_Aim.trigger, "总是\0开火\0开镜\0");
                ImGui::SliderFloat("FOV", &g_Aim.fov, 50.0f, 800.0f, "%.0f");
                ImGui::Checkbox("预判", &g_Aim.predict);
                ImGui::SameLine(0,20);
                ImGui::Checkbox("下坠", &g_Aim.drop);
                ImGui::Checkbox("概率模式", &g_Aim.prob);
                if (g_Aim.prob) { ImGui::SameLine(0,20); ImGui::SliderFloat("命中率", &g_Aim.probRate, 0.05f, 1.0f, "%.2f"); }
                ImGui::Separator();
                ImGui::Text("寄存器适配:");
                ImGui::Combo("StartLoc", &g_Aim.regLoc, "Q0\0Q1\0X1指针\0");
                ImGui::SameLine(0,20);
                ImGui::Combo("StartRot", &g_Aim.regRot, "Q0\0Q1\0X2指针\0");
                if (g_Aim.mode == 2) {
                    ImGui::Separator();
                    ImGui::Text("转储(#%llu):", (unsigned long long)g_Dump.hits);
                    ImGui::Text("X0=%llx X1=%llx", (unsigned long long)g_Dump.x0, (unsigned long long)g_Dump.x1);
                    ImGui::Text("X2=%llx X3=%llx", (unsigned long long)g_Dump.x2, (unsigned long long)g_Dump.x3);
                    ImGui::Text("Q0=%.1f,%.1f,%.1f", g_Dump.q0[0], g_Dump.q0[1], g_Dump.q0[2]);
                    ImGui::Text("Q1=%.1f,%.1f,%.1f", g_Dump.q1[0], g_Dump.q1[1], g_Dump.q1[2]);
                    ImGui::Text("Q2=%.1f,%.1f,%.1f", g_Dump.q2[0], g_Dump.q2[1], g_Dump.q2[2]);
                }
                ImGui::Separator();
                ImGui::Text("状态:");
                ImGui::BulletText("断点: %s", g_bpSet ? "已设置" : "未设置");
                ImGui::BulletText("触发: %llu", (unsigned long long)g_bpHits);
                ImGui::BulletText("目标: %s", g_Target.valid ? "有" : "无");
                ImGui::BulletText("初速: %.0f", g_BulletSpeed);
                if (g_bpSet && ImGui::Button("移除断点")) { driver->hwbp_remove(); g_bpSet = false; }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::SliderFloat("刷新帧率调节", &FPS, 60.0f, 144.0f, "%.2f", 3);
        ImGui::BulletText("进程:%d", pid);
        ImGui::BulletText("矩阵:%lx", Matrix);
        ImGui::BulletText("自身结构:%lx", MySelf);
        ImGui::BulletText("世界:%lx", Arrayaddr);
        ImGui::BulletText("数量:%d", Count);
        ImGui::TextColored(ImVec4(1.0f,0.0f,1.0f,1.0f), "应用平均 %.3f ms/frame (%.1f FPS)", 1000.0f/ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        g_window = ImGui::GetCurrentWindow();
        ImGui::End();
    }
}