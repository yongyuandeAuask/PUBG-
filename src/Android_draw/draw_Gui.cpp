#include "draw.h"
#include "读写.h"
#include "UeTool.h"
#include "nanovg.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

// 用文件内私有别名，彻底避免和头文件全局变量冲突
static long g_类地址 = 0;
static bool g_忽略人机 = false;

extern NVGcontext* vg;
extern int g_nvg_font;
extern int g_font_agency;
extern int g_font_icons;
extern void (*g_nanovg_render_callback)();

#define FONT_DIST ((g_font_agency != -1) ? g_font_agency : g_nvg_font)
#define FONT_NAME g_nvg_font

// ==================== 描边文本（白/黄字+黑边混色）====================
static void NG_OutlinedText(const char* text, float x, float y, NVGcolor c, float size, int fontId, unsigned char outlineAlpha = 160) {
    nvgFontSize(vg, size);
    nvgFontFaceId(vg, fontId);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, outlineAlpha / 2));
    for (int ox = -2; ox <= 2; ox++) for (int oy = -2; oy <= 2; oy++) { if (abs(ox) + abs(oy) < 3) continue; nvgText(vg, x + ox, y + oy, text, NULL); }
    nvgFillColor(vg, nvgRGBA(0, 0, 0, outlineAlpha));
    for (int ox = -1; ox <= 1; ox++) for (int oy = -1; oy <= 1; oy++) { if (!ox && !oy) continue; nvgText(vg, x + ox, y + oy, text, NULL); }
    nvgFillColor(vg, c);
    nvgText(vg, x, y, text, NULL);
}

// ==================== 初始化 ====================
void DrawInit() {
    if (初始化) return;
    pid = getPID("com.rekoo.pubgm");
    if (pid <= 0) { printf("游戏未启动\n"); return; }
    libbase = getModuleBase("libUE4.so");
    if (libbase <= 0) { printf("libUE4.so 未找到\n"); return; }
    初始化 = true;
    printf("初始化成功! libUE4: %lx, pid: %d\n", libbase, pid);
}

// ==================== 更新游戏数据 ====================
void UpdateGameData() {
    if (!初始化) return;
    Matrix = driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1d5f70) + 0xC0) + 0x590;
    Uworld = driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1fb900) + 0x810) + 0x78);
    Uleve = driver->read<uint64_t>(Uworld + 0x30);
    Arrayaddr = driver->read<uint64_t>(Uleve + 0xA0);
    Count = driver->read<int>(Uleve + 0xA8);
    MySelf = driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1fb900) + 0x810) + 0x78) + 0x38) + 0x78) + 0x30) + 0x28c8);
    g_类地址 = driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xec73720) + 0x110);
    memset(matrix, 0, 16 * 4);
    driver->read((uintptr_t)Matrix, matrix, 16 * 4);
}

// ==================== Canvas ESP 绘制 ====================
static void DrawESP_NanoVG() {
    if (!vg || !初始化 || MySelf == 0) return;

    // 顶部中央白色 asuka（AgencyFB 字体）
    NG_OutlinedText("asuka", ::native_window_screen_x / 2.0f, 40.0f, nvgRGBA(255, 255, 255, 255), 30.0f, FONT_DIST);

    int 自己队伍 = driver->read<int>(MySelf + 0x998);
    Vector3A Z;
    driver->read((uintptr_t)(driver->read<uint64_t>(MySelf + 0x208) + 0x1c8), &Z, sizeof(Z));
    PlayerCount = 0;

    for (int i = 0; i < Count; i++) {
        long int Objaddr = driver->read<uint64_t>(Arrayaddr + 0x8 * i);
        if (Objaddr <= 0xffff || Objaddr == 0 || Objaddr <= 0x10000000 || Objaddr % 4 != 0 || Objaddr >= 0x10000000000) continue;
        if (MySelf == Objaddr) continue;
        if (driver->read<float>(Objaddr + 0x2b78) != 479.5f) continue;

        int ClassID = driver->read<int>(Objaddr + 24);
        long FNameEntry = driver->read<uint64_t>(driver->read<uint64_t>(g_类地址 + (ClassID / 0x4000) * 0x8) + (ClassID % 0x4000) * 0x8);
        char ClassName[64] = "";
        driver->read((uintptr_t)(FNameEntry + 0xC), ClassName, 64);
        if (strstr(ClassName, "BPPawn_Escape_Raven") != 0 || strstr(ClassName, "BPPawn_Escape_UAV_C") != 0) continue;

        int 状态 = driver->read<int>(Objaddr + 0x1058);
        if (状态 == 1048592 || 状态 == 1048576) continue;

        int 敌人队伍 = driver->read<int>(Objaddr + 0x998);
        if (敌人队伍 == 自己队伍) continue;

        int botFlag = driver->read<int>(Objaddr + 0xa59);
        bool 是人机 = (botFlag == 16842753 || botFlag == 16843009 || botFlag == 16843008);
        if (g_忽略人机 && 是人机) continue;

        float 当前血量 = driver->read<float>(Objaddr + 0xe60);
        float 最大血量 = driver->read<float>(Objaddr + 0xe64);
        if (最大血量 <= 0) continue;

        PlayerCount++;

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
        float X = r_x - (r_y - r_w) / 4;
        float MIDDLE = X + W / 2;
        float BOTTOM = r_y + W;
        float TOP = r_y - W;

        Vector2A Head, Chest, Pelvis, Left_Shoulder, Right_Shoulder, Left_Elbow, Right_Elbow,
                 Left_Wrist, Right_Wrist, Left_Thigh, Right_Thigh, Left_Knee, Right_Knee, Left_Ankle, Right_Ankle;
        if (DrawIo[4]) {
            long int Mesh = driver->read<uint64_t>(Objaddr + 0x510);
            long int human = Mesh + 0x210;
            long int Bone = driver->read<uint64_t>(Mesh + 0x9a8) + 0x30;
            FMatrix c2wMatrix = TransformToMatrix(getBone(human));
            #define READ_BONE(var, idx) var = WorldToScreen(MarixToVector(MatrixMulti(TransformToMatrix(getBone(Bone + idx * 48)), c2wMatrix)), matrix, camera)
            READ_BONE(Head, 9); READ_BONE(Chest, 5); READ_BONE(Pelvis, 2);
            READ_BONE(Left_Shoulder, 21);  READ_BONE(Right_Shoulder, 44);
            READ_BONE(Left_Elbow, 22);     READ_BONE(Right_Elbow, 45);
            READ_BONE(Left_Wrist, 23);     READ_BONE(Right_Wrist, 46);
            READ_BONE(Left_Thigh, 68);     READ_BONE(Right_Thigh, 72);
            READ_BONE(Left_Knee, 69);      READ_BONE(Right_Knee, 73);
            READ_BONE(Left_Ankle, 70);     READ_BONE(Right_Ankle, 74);
            #undef READ_BONE
        }

        float bottom = DrawIo[4] ? ((Left_Ankle.Y < Right_Ankle.Y) ? Right_Ankle.Y + W / 10 : Left_Ankle.Y + W / 10) : BOTTOM;

        // 方框
        if (DrawIo[1]) {
            nvgBeginPath(vg); nvgRect(vg, X - 1, TOP - 1, W + 2, (bottom - TOP) + 2); nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 60)); nvgStrokeWidth(vg, 4.0f); nvgStroke(vg);
            nvgBeginPath(vg); nvgRect(vg, X, TOP, W, bottom - TOP); nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255)); nvgStrokeWidth(vg, 1.5f); nvgStroke(vg);
        }
        // 射线
        if (DrawIo[3]) {
            nvgBeginPath(vg); nvgMoveTo(vg, px, 130); nvgLineTo(vg, r_x, TOP); nvgStrokeColor(vg, nvgRGBA(255, 0, 0, 255)); nvgStrokeWidth(vg, 1.0f); nvgStroke(vg);
        }
        // 骨骼（0.8 细白线）—— Lambda 语法已修正
        if (DrawIo[4]) {
            auto bone = [](Vector2A a, Vector2A b) {
                nvgBeginPath(vg); nvgMoveTo(vg, a.X, a.Y); nvgLineTo(vg, b.X, b.Y);
                nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255)); nvgStrokeWidth(vg, 0.8f); nvgStroke(vg);
            };
            bone(Head, Chest); bone(Chest, Pelvis);
            bone(Chest, Left_Shoulder);  bone(Left_Shoulder, Left_Elbow);  bone(Left_Elbow, Left_Wrist);
            bone(Chest, Right_Shoulder); bone(Right_Shoulder, Right_Elbow); bone(Right_Elbow, Right_Wrist);
            bone(Pelvis, Left_Thigh);  bone(Left_Thigh, Left_Knee);  bone(Left_Knee, Left_Ankle);
            bone(Pelvis, Right_Thigh); bone(Right_Thigh, Right_Knee); bone(Right_Knee, Right_Ankle);
        }
        // 血条（红填充+黑框+分段线）
        if (DrawIo[6]) {
            float CurHP = 当前血量 > 0 ? 当前血量 : 0;
            float mWidthScale = std::min(0.1f * Distance, 35.0f);
            float mWidth = 80.0f - mWidthScale, mHeight = mWidth * 0.07f;
            float bx = MIDDLE - mWidth / 2.0f, by = TOP - 14.0f;
            nvgBeginPath(vg); nvgRect(vg, bx, by, mWidth * (CurHP / 最大血量), mHeight); nvgFillColor(vg, nvgRGBA(255, 0, 0, 255)); nvgFill(vg);
            nvgBeginPath(vg); nvgRect(vg, bx, by, mWidth, mHeight); nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 255)); nvgStrokeWidth(vg, 1.3f); nvgStroke(vg);
            for (int k = 16; k < mWidth; k += 16) { nvgBeginPath(vg); nvgMoveTo(vg, bx + k, by); nvgLineTo(vg, bx + k, by + mHeight); nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 255)); nvgStrokeWidth(vg, 1.3f); nvgStroke(vg); }
        }
        // 名字（黄字黑边）
        if (DrawIo[5]) {
            char nameBuf[96];
            if (是人机) snprintf(nameBuf, sizeof(nameBuf), "%d 人机", 敌人队伍);
            else { getUTF8(PlayerName, driver->read<uint64_t>(Objaddr + 0x960)); snprintf(nameBuf, sizeof(nameBuf), "%d %s", 敌人队伍, PlayerName); }
            NG_OutlinedText(nameBuf, MIDDLE, TOP - 32, nvgRGBA(255, 255, 0, 255), (float)std::max(5, 12 - (int)(Distance / 40)) * 1.6f, FONT_NAME);
        }
        // 距离（黄字黑边）
        if (DrawIo[2]) {
            char distBuf[32]; snprintf(distBuf, sizeof(distBuf), "%d M", (int)Distance);
            NG_OutlinedText(distBuf, MIDDLE, BOTTOM + 14, nvgRGBA(255, 255, 0, 255), (float)std::max(3, 8 - (int)(Distance / 100)) * 2.0f, FONT_DIST);
        }
    }
}

// ==================== 帧回调（画完清理 GL 状态，保护 ImGui）====================
static void NanoVG_Frame_Callback() {
    if (!vg) return;
    nvgBeginFrame(vg, ::native_window_screen_x, ::native_window_screen_y, 1.0f);
    DrawESP_NanoVG();
    nvgEndFrame(vg);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void DrawPlayer(ImDrawList* draw) {} // ESP 已由 NanoVG 接管

// ==================== ImG
