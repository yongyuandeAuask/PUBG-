// src/Android_draw/draw_Gui.cpp
#include "draw.h"
#include "My_font/zh_Font.h"
#include "读写.h"
#include "UeTool.h"
#include "绘图.h"
#include "nanovg.h"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

extern NVGcontext* vg;
extern int g_nvg_font;
extern int g_font_agency;

static long 类地址 = 0;
static bool 忽略人机 = false;
static int g_health_style = 0;   // 血条样式：0原版渐变 1圆形2D 2莫比乌斯环
static int RealCount = 0;
static int BotCount = 0;

void DrawInit() {
    if (初始化) return;
    pid = getPID("com.rekoo.pubgm");
    if (pid <= 0) { printf("游戏未启动\n"); return; }
    libbase = getModuleBase("libUE4.so");
    if (libbase <= 0) { printf("libUE4.so 未找到\n"); return; }
    初始化 = true;
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
}

static void SetFontNVG(NVGcontext* vg, int fontId) {
    nvgFontFaceId(vg, fontId);
    if (fontId == g_font_agency) {
        nvgAddFallbackFont(vg, "agency", "zh");
    }
}

// 无描边纯文本
void 绘制文本纯NVG(NVGcontext* vg, int fontId, const char* text,
                   Vector2A pos, float fontSize, NVGcolor color, bool isCenter = false)
{
    if (!vg || !text || fontId < 0) return;
    nvgFontSize(vg, fontSize);
    SetFontNVG(vg, fontId);
    nvgTextAlign(vg, (isCenter ? NVG_ALIGN_CENTER : NVG_ALIGN_LEFT) | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, color);
    nvgText(vg, pos.X, pos.Y, text, NULL);
}

// 描边文本（4 对角 1px 黑边）
void DrawOutlinedTextNVG(NVGcontext* vg, int fontId, const char* text,
                         Vector2A pos, float fontSize,
                         NVGcolor color, NVGcolor outlineColor,
                         bool isCenter = false, float outlineWidth = 1.0f)
{
    if (!vg || !text || fontId < 0) return;
    nvgFontSize(vg, fontSize);
    SetFontNVG(vg, fontId);
    nvgTextAlign(vg, (isCenter ? NVG_ALIGN_CENTER : NVG_ALIGN_LEFT) | NVG_ALIGN_TOP);

    nvgFillColor(vg, outlineColor);
    const float o = outlineWidth;
    nvgText(vg, pos.X - o, pos.Y - o, text, NULL);
    nvgText(vg, pos.X + o, pos.Y - o, text, NULL);
    nvgText(vg, pos.X - o, pos.Y + o, text, NULL);
    nvgText(vg, pos.X + o, pos.Y + o, text, NULL);

    nvgFillColor(vg, color);
    nvgText(vg, pos.X, pos.Y, text, NULL);
}

static void DrawLineNVG(NVGcontext* vg, float x1, float y1, float x2, float y2,
                        NVGcolor color, float thickness = 1.5f)
{
    if (!vg) return;
    nvgBeginPath(vg);
    nvgMoveTo(vg, x1, y1);
    nvgLineTo(vg, x2, y2);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, thickness);
    nvgStroke(vg);
}

static void DrawHexagonStarNVG(NVGcontext* vg, float x, float y, float size,
                               float rotation, float thickness = 1.5f)
{
    if (!vg) return;
    NVGcolor white = nvgRGBA(255, 255, 255, 255);
    NVGcolor black = nvgRGBA(0, 0, 0, 255);

    float ptx[6], pty[6];
    for (int i = 0; i < 6; i++) {
        float angle = rotation + 2.0f * NVG_PI * i / 6.0f;
        ptx[i] = x + size * cosf(angle);
        pty[i] = y + size * sinf(angle);
    }

    float bt = thickness + 2.0f;
    DrawLineNVG(vg, ptx[0], pty[0], ptx[2], pty[2], black, bt);
    DrawLineNVG(vg, ptx[2], pty[2], ptx[4], pty[4], black, bt);
    DrawLineNVG(vg, ptx[4], pty[4], ptx[0], pty[0], black, bt);
    DrawLineNVG(vg, ptx[1], pty[1], ptx[3], pty[3], black, bt);
    DrawLineNVG(vg, ptx[3], pty[3], ptx[5], pty[5], black, bt);
    DrawLineNVG(vg, ptx[5], pty[5], ptx[1], pty[1], black, bt);

    DrawLineNVG(vg, ptx[0], pty[0], ptx[2], pty[2], white, thickness);
    DrawLineNVG(vg, ptx[2], pty[2], ptx[4], pty[4], white, thickness);
    DrawLineNVG(vg, ptx[4], pty[4], ptx[0], pty[0], white, thickness);
    DrawLineNVG(vg, ptx[1], pty[1], ptx[3], pty[3], white, thickness);
    DrawLineNVG(vg, ptx[3], pty[3], ptx[5], pty[5], white, thickness);
    DrawLineNVG(vg, ptx[5], pty[5], ptx[1], pty[1], white, thickness);
}

static void DrawLogoNVG(NVGcontext* vg, float x, float y, float size)
{
    static float rotation = 0.0f;
    rotation += 0.05f;
    DrawHexagonStarNVG(vg, x, y, size, rotation, 1.5f);
}

void DrawPlayerNVG(NVGcontext* vg) {
    if (!初始化 || MySelf == 0 || vg == nullptr) return;

    auto onScreen = [&](const Vector2A& p) {
        return p.X > 0 && p.Y > 0 && p.X < (float)abs_ScreenX && p.Y < (float)abs_ScreenY;
    };

    int 自己队伍 = driver->read<int>(MySelf + 0x998);
    Vector3A Z;
    driver->read((uintptr_t)(driver->read<uint64_t>(MySelf + 0x208) + 0x1c8), &Z, sizeof(Z));

    PlayerCount = 0; RealCount = 0; BotCount = 0;

    for (int i = 0; i < Count; i++) {
        long int Objaddr = driver->read<uint64_t>(Arrayaddr + 0x8 * i);
        if (Objaddr <= 0xffff || Objaddr == 0 || Objaddr <= 0x10000000 || Objaddr % 4 != 0 || Objaddr >= 0x10000000000) continue;
        if (MySelf == Objaddr) continue;

        int ClassID = driver->read<int>(Objaddr + 24);
        long FNameEntry = driver->read<uint64_t>(driver->read<uint64_t>(类地址 + (ClassID / 0x4000) * 0x8) + (ClassID % 0x4000) * 0x8);
        char ClassName[64] = "";
        driver->read((uintptr_t)(FNameEntry + 0xC), ClassName, 64);
        if (strstr(ClassName, "BPPawn_Escape_Raven") != 0 || strstr(ClassName, "BPPawn_Escape_UAV_C") != 0) continue;

        float 玩家标志 = driver->read<float>(Objaddr + 0x2b78);
        bool isDog = (strstr(ClassName, "AIMob_PatrolDog_C") != 0);
        bool isHunger = (strstr(ClassName, "BPPawn_HungerH_C") != 0) ||
                        (strstr(ClassName, "BPPawn_HungerB_C") != 0);
        if (玩家标志 != 479.5f && !isDog && !isHunger) continue;

        int 状态 = driver->read<int>(Objaddr + 0x1058);
        if (状态 == 1048592 || 状态 == 1048576) continue;

        int 敌人队伍 = driver->read<int>(Objaddr + 0x998);
        if (敌人队伍 == 自己队伍) continue;

        int botFlag = driver->read<int>(Objaddr + 0xa59);
        bool isBot = isDog || isHunger ||
                     (敌人队伍 != 0 && (botFlag == 16842753 || botFlag == 16843009 || botFlag == 16843008));
        if (忽略人机 && isBot) continue;

        float 当前血量 = driver->read<float>(Objaddr + 0xe60);
        float 最大血量 = driver->read<float>(Objaddr + 0xe64);
        if (最大血量 <= 0) continue;

        PlayerCount++;
        if (isBot) BotCount++; else RealCount++;

        Vector3A D;
        driver->read((uintptr_t)(driver->read<uint64_t>(Objaddr + 0x208) + 0x1c8), &D, sizeof(D));

        float camera = matrix[3] * D.X + matrix[7] * D.Y + matrix[11] * D.Z + matrix[15];
        if (camera <= 0.001f) continue;
        float Distance = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2) + pow(D.Z - Z.Z, 2)) * 0.01f;
        if (Distance > 500 || Distance <= 0) continue;

        float r_x = px + (matrix[0] * D.X + matrix[4] * D.Y + matrix[8] * D.Z + matrix[12]) / camera * px;
        float r_y = py - (matrix[1] * D.X + matrix[5] * D.Y + matrix[9] * (D.Z - 5) + matrix[13]) / camera * py;
        float r_w = py - (matrix[1] * D.X + matrix[5] * D.Y + matrix[9] * (D.Z + 205) + matrix[13]) / camera * py;
        float W = (r_y - r_w) / 2;
        if (W <= 0) continue;
        float MIDDLE = r_x;
        float TOP_FALLBACK = r_y - W;
        float BOTTOM_FALLBACK = r_y + W;

        // 骨骼索引（固定：绘制默认表）
        int idx_head, idx_chest, idx_pelvis, idx_lsh, idx_rsh, idx_lelb, idx_relb,
            idx_lw, idx_rw, idx_lth, idx_rth, idx_lk, idx_rk, idx_la, idx_ra;

        if (isDog) {
            idx_head = 5; idx_chest = 3; idx_pelvis = 0;
            idx_lsh = 7;  idx_rsh = 11; idx_lelb = 8;  idx_relb = 12;
            idx_lw = 9;   idx_rw = 13;
            idx_lth = 14; idx_rth = 18; idx_lk = 15; idx_rk = 19;
            idx_la = 16;  idx_ra = 20;
        } else if (isHunger) {
            idx_head = 5; idx_chest = 3; idx_pelvis = 0;
            idx_lsh = 11; idx_rsh = 18; idx_lelb = 12; idx_relb = 19;
            idx_lw = 13;  idx_rw = 20;
            idx_lth = 24; idx_rth = 29; idx_lk = 25; idx_rk = 30;
            idx_la = 26;  idx_ra = 31;
        } else {
            idx_head = 5; idx_chest = 4; idx_pelvis = 0;
            idx_lsh = 13; idx_rsh = 34; idx_lelb = 14; idx_relb = 35;
            idx_lw = 16;  idx_rw = 37;
            idx_lth = 54; idx_rth = 58; idx_lk = 55; idx_rk = 59;
            idx_la = 56;  idx_ra = 60;
        }

        Vector2A Head, Chest, Pelvis, Left_Shoulder, Right_Shoulder,
                 Left_Elbow, Right_Elbow, Left_Wrist, Right_Wrist,
                 Left_Thigh, Right_Thigh, Left_Knee, Right_Knee,
                 Left_Ankle, Right_Ankle;
        bool bonesOk = false;

        if (DrawIo[4]) {
            long int Mesh = driver->read<uint64_t>(Objaddr + 0x510);
            if (Mesh > 0x10000000 && Mesh < 0x10000000000) {
                long int boneArrayPtr = driver->read<uint64_t>(Mesh + 0x9a8);
                int BoneCount = driver->read<int>(Mesh + 0x9a8 + 8);
                if (boneArrayPtr > 0x10000000 && boneArrayPtr < 0x10000000000 &&
                    BoneCount > 0 && BoneCount < 200) {

                    long int human = Mesh + 0x210;
                    long int Bone = boneArrayPtr + 0x30;

                    FTransform meshtrans = getBone(human);
                    FMatrix c2wMatrix = TransformToMatrix(meshtrans);

                    FTransform headtrans = getBone(Bone + idx_head * 48);
                    Vector3A HeadPos = MarixToVector(MatrixMulti(TransformToMatrix(headtrans), c2wMatrix));
                    HeadPos.Z += 7.0f;
                    Head = WorldToScreen(HeadPos, matrix, camera);

                    FTransform chesttrans = getBone(Bone + idx_chest * 48);
                    Chest = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(chesttrans), c2wMatrix)), matrix, camera);

                    FTransform pelvistrans = getBone(Bone + idx_pelvis * 48);
                    Pelvis = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(pelvistrans), c2wMatrix)), matrix, camera);

                    FTransform lshtrans = getBone(Bone + idx_lsh * 48);
                    Left_Shoulder = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(lshtrans), c2wMatrix)), matrix, camera);

                    FTransform rshtrans = getBone(Bone + idx_rsh * 48);
                    Right_Shoulder = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(rshtrans), c2wMatrix)), matrix, camera);

                    FTransform lelbtrans = getBone(Bone + idx_lelb * 48);
                    Left_Elbow = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(lelbtrans), c2wMatrix)), matrix, camera);

                    FTransform relbtrans = getBone(Bone + idx_relb * 48);
                    Right_Elbow = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(relbtrans), c2wMatrix)), matrix, camera);

                    FTransform lwtrans = getBone(Bone + idx_lw * 48);
                    Left_Wrist = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(lwtrans), c2wMatrix)), matrix, camera);

                    FTransform rwtrans = getBone(Bone + idx_rw * 48);
                    Right_Wrist = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(rwtrans), c2wMatrix)), matrix, camera);

                    FTransform Llshtrans = getBone(Bone + idx_lth * 48);
                    Left_Thigh = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(Llshtrans), c2wMatrix)), matrix, camera);

                    FTransform Lrshtrans = getBone(Bone + idx_rth * 48);
                    Right_Thigh = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(Lrshtrans), c2wMatrix)), matrix, camera);

                    FTransform Llelbtrans = getBone(Bone + idx_lk * 48);
                    Left_Knee = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(Llelbtrans), c2wMatrix)), matrix, camera);

                    FTransform Lrelbtrans = getBone(Bone + idx_rk * 48);
                    Right_Knee = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(Lrelbtrans), c2wMatrix)), matrix, camera);

                    FTransform Llwtrans = getBone(Bone + idx_la * 48);
                    Left_Ankle = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(Llwtrans), c2wMatrix)), matrix, camera);

                    FTransform Lrwtrans = getBone(Bone + idx_ra * 48);
                    Right_Ankle = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(Lrwtrans), c2wMatrix)), matrix, camera);

                    bonesOk = onScreen(Head) && onScreen(Chest) && onScreen(Pelvis) &&
                              onScreen(Left_Shoulder) && onScreen(Right_Shoulder) &&
                              onScreen(Left_Elbow) && onScreen(Right_Elbow) &&
                              onScreen(Left_Wrist) && onScreen(Right_Wrist) &&
                              onScreen(Left_Thigh) && onScreen(Right_Thigh) &&
                              onScreen(Left_Knee) && onScreen(Right_Knee) &&
                              onScreen(Left_Ankle) && onScreen(Right_Ankle);
                }
            }
        }

        float headX = (bonesOk && Head.X > 0) ? Head.X : MIDDLE;
        float headY = (bonesOk && Head.Y > 0) ? Head.Y : TOP_FALLBACK;
        float left  = headX - W * 0.6f;
        float right = headX + W * 0.6f;
        float top   = (bonesOk && Head.Y > 0) ? (Head.Y - W / 5.0f) : TOP_FALLBACK;
        float bottom = (bonesOk) ? ((Left_Ankle.Y < Right_Ankle.Y) ? Right_Ankle.Y + W / 10.0f : Left_Ankle.Y + W / 10.0f) : BOTTOM_FALLBACK;
        float pelvisX = (bonesOk && Pelvis.X > 0) ? Pelvis.X : headX;
        float pelvisY = (bonesOk && Pelvis.Y > 0) ? Pelvis.Y : (top + (bottom - top) * 0.5f);

        NVGcolor COL_GREEN = nvgRGBA(0, 255, 0, 255);
        NVGcolor COL_RED = nvgRGBA(255, 0, 0, 255);
        NVGcolor COL_LIGHTBLUE = nvgRGBA(173, 216, 230, 255);
        NVGcolor COL_CYAN = nvgRGBA(0, 255, 255, 255);
        NVGcolor COL_WHITE = nvgRGBA(255, 255, 255, 255);
        NVGcolor COL_BLACK = nvgRGBA(0, 0, 0, 255);

        // 方框
        if (DrawIo[1]) {
            NVGcolor boxColor = (isBot) ? COL_GREEN : (当前血量 <= 0 ? COL_RED : COL_LIGHTBLUE);
            DrawLineNVG(vg, left, top, right, top, boxColor, 1.5f);
            DrawLineNVG(vg, right, top, right, bottom, boxColor, 1.5f);
            DrawLineNVG(vg, right, bottom, left, bottom, boxColor, 1.5f);
            DrawLineNVG(vg, left, bottom, left, top, boxColor, 1.5f);
        }

        // 射线
        if (DrawIo[3]) {
            bool off = (headX < 0 || headX > (float)abs_ScreenX || headY < 0 || headY > (float)abs_ScreenY);
            NVGcolor rayColor = isBot ? COL_GREEN : (off ? COL_RED : COL_LIGHTBLUE);
            DrawLineNVG(vg, (float)abs_ScreenX * 0.5f, 73.0f, headX, headY - 70.0f, rayColor, isBot ? 1.5f : 1.0f);
        }

        // 骨骼
        if (DrawIo[4] && bonesOk) {
            NVGcolor boneColor = isBot ? COL_CYAN : COL_LIGHTBLUE;
            nvgBeginPath(vg);
            nvgCircle(vg, Head.X, Head.Y, W / 5.0f);
            nvgStrokeColor(vg, boneColor);
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);

            DrawLineNVG(vg, Head.X, Head.Y, Chest.X, Chest.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Chest.X, Chest.Y, Pelvis.X, Pelvis.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Chest.X, Chest.Y, Left_Shoulder.X, Left_Shoulder.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Chest.X, Chest.Y, Right_Shoulder.X, Right_Shoulder.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Left_Shoulder.X, Left_Shoulder.Y, Left_Elbow.X, Left_Elbow.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Right_Shoulder.X, Right_Shoulder.Y, Right_Elbow.X, Right_Elbow.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Left_Elbow.X, Left_Elbow.Y, Left_Wrist.X, Left_Wrist.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Right_Elbow.X, Right_Elbow.Y, Right_Wrist.X, Right_Wrist.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Pelvis.X, Pelvis.Y, Left_Thigh.X, Left_Thigh.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Pelvis.X, Pelvis.Y, Right_Thigh.X, Right_Thigh.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Left_Thigh.X, Left_Thigh.Y, Left_Knee.X, Left_Knee.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Right_Thigh.X, Right_Thigh.Y, Right_Knee.X, Right_Knee.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Left_Knee.X, Left_Knee.Y, Left_Ankle.X, Left_Ankle.Y, boneColor, 1.5f);
            DrawLineNVG(vg, Right_Knee.X, Right_Knee.Y, Right_Ankle.X, Right_Ankle.Y, boneColor, 1.5f);
        }

        // 距离
        if (DrawIo[2]) {
            char distBuf[16];
            snprintf(distBuf, sizeof(distBuf), "%d m", (int)Distance);
            float yPos = bottom + 15;
            if (yPos > (float)abs_ScreenY - 25) yPos = (float)abs_ScreenY - 25;
            DrawOutlinedTextNVG(vg, g_font_agency, distBuf, {headX, yPos}, 25.0f,
                                COL_WHITE, COL_BLACK, true, 1.0f);
        }

        // 队伍+名字
        if (DrawIo[5]) {
            char tagBuf[96];
            if (isBot) {
                snprintf(tagBuf, sizeof(tagBuf), "[%d] 人机", 敌人队伍);
            } else {
                getUTF8(PlayerName, driver->read<uint64_t>(Objaddr + 0x960));
                snprintf(tagBuf, sizeof(tagBuf), "[%d] %s", 敌人队伍, PlayerName);
            }
            float yPos = pelvisY - 50;
            if (yPos < 0) yPos = 0;
            DrawOutlinedTextNVG(vg, g_font_agency, tagBuf, {pelvisX, yPos}, 18.0f,
                                COL_WHITE, COL_BLACK, true, 1.0f);
        }

        // 敌人地址
        if (DrawIo[7]) {
            char buf[32];
            sprintf(buf, "0x%lx", Objaddr);
            DrawOutlinedTextNVG(vg, g_font_agency, buf, {headX, bottom}, 16.0f,
                                COL_WHITE, COL_BLACK, true, 1.0f);
        }

        // ==================== 血量条（3 样式切换）====================
        if (DrawIo[6]) {
            float CurHP = 当前血量;
            float MaxHP = 最大血量;
            if (MaxHP <= 0) MaxHP = 100.0f;
            if (CurHP < 0) CurHP = 0;
            if (CurHP > MaxHP) CurHP = MaxHP;
            float hp_ratio = CurHP / MaxHP;
            int hp_percent = (int)(hp_ratio * 100);
            float cx2 = headX;
            float cy2 = headY - 70.0f;

            if (g_health_style == 1) {
                // ===== 圆形 2D 血条 =====
                NVGcolor hpColor;
                if (hp_percent < 25)      hpColor = nvgRGBA(255, 0, 0, 255);
                else if (hp_percent < 50) hpColor = nvgRGBA(255, 165, 0, 255);
                else if (hp_percent < 75) hpColor = nvgRGBA(219, 255, 0, 255);
                else                      hpColor = nvgRGBA(0, 255, 0, 255);

                nvgBeginPath(vg);
                nvgArc(vg, cx2, cy2, 40.0f, -NVG_PI / 2.0f, -NVG_PI / 2.0f + 2.0f * NVG_PI * hp_ratio, NVG_CW);
                nvgStrokeColor(vg, hpColor);
                nvgStrokeWidth(vg, 6.0f);
                nvgLineCap(vg, NVG_ROUND);
                nvgStroke(vg);

                char hpBuf[8];
                snprintf(hpBuf, sizeof(hpBuf), "%d%%", hp_percent);
                绘制文本纯NVG(vg, g_font_agency, hpBuf, {cx2, cy2}, 16.0f, COL_WHITE, true);

            } else if (g_health_style == 2) {
                // ===== 莫比乌斯环血条 =====
                float dr = 30.0f * (0.8f + 0.2f * hp_ratio);
                float dtw = 0.8f * (1.0f - hp_ratio);
                static float GameTime = 0.0f;
                GameTime += 0.0167f;
                if (GameTime > 6.28319f) GameTime -= 6.28319f;

                NVGcolor base = nvgRGBAf((1.0f - hp_ratio) * 0.8f, hp_ratio * 0.9f, 0.2f, 0.7f);
                float thick = 4.0f * (0.8f + 0.2f * hp_ratio);

                float px0 = 0, py0 = 0;
                for (int i = 0; i <= 80; i++) {
                    float t = (float)i / 80.0f * 2.0f * NVG_PI;
                    float sx = cx2 + dr * cosf(t);
                    float sy = cy2 + dr * sinf(t) * 0.45f + dr * dtw * t / (2.0f * NVG_PI);
                    if (i > 0) DrawLineNVG(vg, px0, py0, sx, sy, base, thick);
                    px0 = sx; py0 = sy;
                }

                for (int i = 0; i < 20; i++) {
                    float t = (float)i / 20.0f * 2.0f * NVG_PI;
                    float fr = dr * 0.6f;
                    float a = t + GameTime;
                    float sx = cx2 + fr * cosf(a);
                    float sy = cy2 + fr * sinf(a) * 0.45f + dr * dtw * t / (2.0f * NVG_PI);
                    nvgBeginPath(vg);
                    nvgCircle(vg, sx, sy, 3.0f);
                    nvgFillColor(vg, nvgRGBAf(1.0f, 0.8f, 0.2f, 0.9f));
                    nvgFill(vg);
                }

            } else {
                // ===== 原版渐变条 =====
                float r = std::min(((510.f * (MaxHP - CurHP)) / MaxHP) / 255.f, 1.f);
                float g = std::min(((510.f * CurHP) / MaxHP) / 255.f, 1.f);
                float b = 0.f;
                float a = 0.85f;

                bool isDowned = (CurHP <= 0.01f);
                if (isDowned) {
                    r = 0.63f; g = 0.82f; b = 0.42f; a = 0.9f;
                    CurHP = 0;
                }

                float mWidthScale = std::min(0.1f * Distance, 35.f);
                float mWidth = 80.f - mWidthScale;
                float mHeight = mWidth * 0.07f;
                if (mHeight < 3.0f) mHeight = 3.0f;

                float barX = headX - (mWidth / 2.0f);
                float barY = top - (mHeight * 4.5f);

                float fillWidth = (CurHP / MaxHP) * mWidth;
                nvgBeginPath(vg);
                nvgRect(vg, barX, barY, fillWidth, mHeight);
                nvgFillColor(vg, nvgRGBAf(r, g, b, a));
                nvgFill(vg);

                nvgStrokeColor(vg, COL_BLACK);
                nvgStrokeWidth(vg, 1.5f);

                nvgBeginPath(vg);
                nvgRect(vg, barX, barY, mWidth, mHeight);
                nvgStroke(vg);

                for (int i = 1; i <= 4; i++) {
                    float lineX = barX + (mWidth / 5.0f) * i;
                    DrawLineNVG(vg, lineX, barY, lineX, barY + mHeight, COL_BLACK, 1.5f);
                }
            }
        }
    }
}

// ==================== NanoVG 画布帧 ====================
void DrawCanvas() {
    if (!vg) return;
    nvgBeginFrame(vg, (float)abs_ScreenX, (float)abs_ScreenY, 1.0f);

    float cx = (float)abs_ScreenX * 0.5f;

    DrawPlayerNVG(vg);

    DrawLogoNVG(vg, (float)abs_ScreenX / 4.0f, (float)abs_ScreenY / 10.0f, 35.0f);

    绘制文本纯NVG(vg, g_font_agency, "Asuka追锁", {cx, 45.0f}, 48.0f, nvgRGBA(255, 255, 255, 255), true);
    绘制文本纯NVG(vg, g_font_agency, "@Asuka1314", {cx, 95.0f}, 26.0f, nvgRGBA(220, 220, 220, 255), true);

    char infoBuf[64];
    snprintf(infoBuf, sizeof(infoBuf), "真人: %d  人机: %d", RealCount, BotCount);
    绘制文本纯NVG(vg, g_font_agency, infoBuf, {cx, 130.0f}, 24.0f, nvgRGBA(255, 255, 255, 255), true);

    nvgEndFrame(vg);
}

// ==================== UI ====================
void Layout_tick_UI(bool* main_thread_flag) {
    UpdateGameData();
    static bool show_another_window = false;
    {
        static float f = 0.0f;
        static int counter = 0;
        static int style_idx = 0;
        static ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        ImGui::Begin("ImGui-UE4", main_thread_flag);
        if (::permeate_record_ini) {
            ImGui::SetWindowPos({ LastCoordinate.Pos_x, LastCoordinate.Pos_y });
            ImGui::SetWindowSize({ LastCoordinate.Size_x, LastCoordinate.Size_y });
            permeate_record_ini = false;
        }
        ImGui::Text("渲染模式 : %s, gui版本 : %s", graphics->RenderName, IMGUI_VERSION);
        if (ImGui::Combo("##主题", &style_idx, "白色主题\0蓝色主题\0紫色主题\0")) {
            switch (style_idx) {
                case 0: ImGui::StyleColorsLight(); break;
                case 1: ImGui::StyleColorsDark(); break;
                case 2: ImGui::StyleColorsClassic(); break;
            }
        }
        if (ImGui::Checkbox("过录制", &::permeate_record)) {
            ::permeate_record_ini = true;
        }
        if (ImGui::Button("初始化绘制", ImVec2(ImGui::GetContentRegionAvail().x, 50))) {
            DrawInit();
        }
        ImGui::ItemSize(ImVec2(0, 5));
        ImGui::Checkbox("显示方框", &DrawIo[1]);
        ImGui::SameLine(0, 40);
        ImGui::Checkbox("显示距离", &DrawIo[2]);
        ImGui::SameLine(0, 40);
        ImGui::Checkbox("显示射线", &DrawIo[3]);
        ImGui::Checkbox("显示骨骼", &DrawIo[4]);
        ImGui::SameLine(0, 40);
        ImGui::Checkbox("显示信息", &DrawIo[5]);
        ImGui::SameLine(0, 40);
        ImGui::Checkbox("显示血量", &DrawIo[6]);
        if (DrawIo[6]) {
            ImGui::SameLine(0, 20);
            ImGui::Combo("##HealthStyle", &g_health_style, "原版渐变条\0圆形2D血条\0莫比乌斯环\0");
        }
        ImGui::Checkbox("敌人地址", &DrawIo[7]);
        ImGui::SameLine(0, 40);
        ImGui::Checkbox("忽略人机", &忽略人机);
        ImGui::SliderFloat("刷新帧率调节", &FPS, 60.0f, 144.0f, "%.2f", 3);
        ImGui::BulletText("进程:%d", pid);
        ImGui::BulletText("矩阵:%lx", Matrix);
        ImGui::BulletText("自身结构:%lx", MySelf);
        ImGui::BulletText("世界:%lx", Arrayaddr);
        ImGui::BulletText("数量:%d", Count);
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "应用平均 %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        g_window = ImGui::GetCurrentWindow();
        ImGui::End();
    }
}