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

extern NVGcontext* vg;
extern int g_nvg_font;
extern int g_font_agency;

static long 类地址 = 0;
static bool 忽略人机 = false;

void DrawInit() {
    if (初始化) return;
    pid = getPID("com.rekoo.pubgm");
    if (pid <= 0) {
        printf("游戏未启动\n");
        return;
    }
    libbase = getModuleBase("libUE4.so");
    if (libbase <= 0) {
        printf("libUE4.so 未找到\n");
        return;
    }
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

// ==================== NanoVG 版 DrawOutlinedText ====================
void DrawOutlinedTextNVG(NVGcontext* vg, int fontId, const char* text,
                         Vector2A pos, float fontSize,
                         NVGcolor color, NVGcolor outlineColor,
                         bool isCenter = false, float outlineWidth = 2.0f)
{
    if (!vg || !text || fontId < 0) return;

    nvgFontSize(vg, fontSize);
    nvgFontFaceId(vg, fontId);
    nvgTextAlign(vg, (isCenter ? NVG_ALIGN_CENTER : NVG_ALIGN_LEFT) | NVG_ALIGN_TOP);

    nvgFillColor(vg, outlineColor);
    const float ox = outlineWidth;
    const float oy = outlineWidth;
    nvgText(vg, pos.X - ox, pos.Y,      text, NULL);
    nvgText(vg, pos.X + ox, pos.Y,      text, NULL);
    nvgText(vg, pos.X,      pos.Y - oy, text, NULL);
    nvgText(vg, pos.X,      pos.Y + oy, text, NULL);
    nvgText(vg, pos.X - ox, pos.Y - oy, text, NULL);
    nvgText(vg, pos.X + ox, pos.Y - oy, text, NULL);
    nvgText(vg, pos.X - ox, pos.Y + oy, text, NULL);
    nvgText(vg, pos.X + ox, pos.Y + oy, text, NULL);

    nvgFillColor(vg, color);
    nvgText(vg, pos.X, pos.Y, text, NULL);
}

// ==================== 绘制玩家（NanoVG 版 + 绘制.h 索引）====================
void DrawPlayerNVG(NVGcontext* vg) {
    if (!初始化 || MySelf == 0 || vg == nullptr) return;

    auto validPt = [&](const Vector2A& p) {
        return p.X > -100.0f && p.X < (float)abs_ScreenX + 100.0f &&
               p.Y > -100.0f && p.Y < (float)abs_ScreenY + 100.0f;
    };

    int 自己队伍 = driver->read<int>(MySelf + 0x998);
    Vector3A Z;
    driver->read((uintptr_t)(driver->read<uint64_t>(MySelf + 0x208) + 0x1c8), &Z, sizeof(Z));

    PlayerCount = 0;
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

        if (忽略人机) {
            int botFlag = driver->read<int>(Objaddr + 0xa59);
            if (敌人队伍 && (botFlag == 16842753 || botFlag == 16843009 || botFlag == 16843008)) continue;
        }

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
        float Y = r_y;
        float MIDDLE = X + W / 2;
        float BOTTOM = Y + W;
        float TOP = Y - W;

        // ==================== 骨骼索引（完整移植自 绘制.h）====================
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
        } else if (敌人队伍 < 101) {
            idx_head = 5; idx_chest = 4; idx_pelvis = 0;
            idx_lsh = 11; idx_rsh = 32; idx_lelb = 12; idx_relb = 33;
            idx_lw = 63;  idx_rw = 62;
            idx_lth = 52; idx_rth = 56; idx_lk = 53; idx_rk = 57;
            idx_la = 54;  idx_ra = 58;
        } else if (敌人队伍 >= 996 && 玩家标志 == 479.5f) {
            idx_head = 5; idx_chest = 4; idx_pelvis = 0;
            idx_lsh = 7;  idx_rsh = 13; idx_lelb = 8;  idx_relb = 14;
            idx_lw = 9;   idx_rw = 15;
            idx_lth = 18; idx_rth = 21; idx_lk = 19; idx_rk = 22;
            idx_la = 20;  idx_ra = 23;
        } else if (driver->read<float>(Objaddr + 0x0) == 200.0f) {
            idx_head = 5; idx_chest = 3; idx_pelvis = 0;
            idx_lsh = 7;  idx_rsh = 11; idx_lelb = 8;  idx_relb = 12;
            idx_lw = 9;   idx_rw = 13;
            idx_lth = 15; idx_rth = 19; idx_lk = 16; idx_rk = 20;
            idx_la = 17;  idx_ra = 21;
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

                    bonesOk = validPt(Head) && validPt(Chest) && validPt(Pelvis) &&
                              validPt(Left_Shoulder) && validPt(Right_Shoulder) &&
                              validPt(Left_Elbow) && validPt(Right_Elbow) &&
                              validPt(Left_Wrist) && validPt(Right_Wrist) &&
                              validPt(Left_Thigh) && validPt(Right_Thigh) &&
                              validPt(Left_Knee) && validPt(Right_Knee) &&
                              validPt(Left_Ankle) && validPt(Right_Ankle);
                }
            }
        }

        float bottom = (DrawIo[4] && bonesOk) ? ((Left_Ankle.Y < Right_Ankle.Y) ? Right_Ankle.Y + W / 10 : Left_Ankle.Y + W / 10) : BOTTOM;

        // 距离（修复：标在敌人脚下，居中）
        if (DrawIo[2]) {
            char distBuf[16];
            snprintf(distBuf, sizeof(distBuf), "%d M", (int)Distance);
            DrawOutlinedTextNVG(vg, g_nvg_font, distBuf, {MIDDLE, bottom + 6}, 24.0f,
                                nvgRGBA(255, 255, 255, 255), nvgRGBA(0, 0, 0, 255), true, 2.0f);
        }

        // 方框
        if (DrawIo[1]) {
            nvgBeginPath(vg);
            nvgRect(vg, X, TOP, W, BOTTOM - TOP);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);
        }

        // 射线
        if (DrawIo[3]) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, px, 130.0f);
            nvgLineTo(vg, r_x, TOP);
            nvgStrokeColor(vg, nvgRGBA(255, 0, 0, 255));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
        }

        // 敌人地址
        if (DrawIo[7]) {
            char buf[32];
            sprintf(buf, "0x%lx", Objaddr);
            DrawOutlinedTextNVG(vg, g_nvg_font, buf, {MIDDLE, BOTTOM}, 16.0f,
                                nvgRGBA(239, 241, 245, 255), nvgRGBA(0, 0, 0, 255), true, 1.5f);
        }

        // 骨骼
        if (DrawIo[4] && bonesOk) {
            NVGcolor boneColor = nvgRGBA(255, 255, 0, 255);
            nvgBeginPath(vg);
            nvgCircle(vg, Head.X, Head.Y, W / 5.0f);
            nvgStrokeColor(vg, boneColor);
            nvgStrokeWidth(vg, 2.5f);
            nvgStroke(vg);

            auto drawBoneLine = [&](float x1, float y1, float x2, float y2) {
                nvgBeginPath(vg);
                nvgMoveTo(vg, x1, y1);
                nvgLineTo(vg, x2, y2);
                nvgStrokeColor(vg, boneColor);
                nvgStrokeWidth(vg, 2.5f);
                nvgStroke(vg);
            };

            drawBoneLine(Head.X, Head.Y, Chest.X, Chest.Y);
            drawBoneLine(Chest.X, Chest.Y, Pelvis.X, Pelvis.Y);
            drawBoneLine(Chest.X, Chest.Y, Left_Shoulder.X, Left_Shoulder.Y);
            drawBoneLine(Chest.X, Chest.Y, Right_Shoulder.X, Right_Shoulder.Y);
            drawBoneLine(Left_Shoulder.X, Left_Shoulder.Y, Left_Elbow.X, Left_Elbow.Y);
            drawBoneLine(Right_Shoulder.X, Right_Shoulder.Y, Right_Elbow.X, Right_Elbow.Y);
            drawBoneLine(Left_Elbow.X, Left_Elbow.Y, Left_Wrist.X, Left_Wrist.Y);
            drawBoneLine(Right_Elbow.X, Right_Elbow.Y, Right_Wrist.X, Right_Wrist.Y);
            drawBoneLine(Pelvis.X, Pelvis.Y, Left_Thigh.X, Left_Thigh.Y);
            drawBoneLine(Pelvis.X, Pelvis.Y, Right_Thigh.X, Right_Thigh.Y);
            drawBoneLine(Left_Thigh.X, Left_Thigh.Y, Left_Knee.X, Left_Knee.Y);
            drawBoneLine(Right_Thigh.X, Right_Thigh.Y, Right_Knee.X, Right_Knee.Y);
            drawBoneLine(Left_Knee.X, Left_Knee.Y, Left_Ankle.X, Left_Ankle.Y);
            drawBoneLine(Right_Knee.X, Right_Knee.Y, Right_Ankle.X, Right_Ankle.Y);
        }

        // 名字
        if (DrawIo[5]) {
            getUTF8(PlayerName, driver->read<uint64_t>(Objaddr + 0x960));
            DrawOutlinedTextNVG(vg, g_nvg_font, PlayerName, {MIDDLE, TOP - 55}, 28.0f,
                                nvgRGBA(248, 248, 255, 255), nvgRGBA(0, 0, 0, 255), true, 2.0f);
        }

        // 血量条
        if (DrawIo[6]) {
            float healthPercent = 最大血量 > 0 ? (当前血量 / 最大血量) : 0.0f;
            float barX = X;
            float barY = TOP - 16 - 8;
            float barWidth = W;
            float barHeight = 8;

            nvgBeginPath(vg);
            nvgRect(vg, barX, barY, barWidth, barHeight);
            nvgFillColor(vg, nvgRGBA(60, 60, 60, 255));
            nvgFill(vg);

            float fillWidth = barWidth * healthPercent;
            nvgBeginPath(vg);
            nvgRect(vg, barX, barY, fillWidth, barHeight);
            nvgFillColor(vg, nvgRGBA(10, 240, 10, 255));
            nvgFill(vg);

            char healthText[16];
            sprintf(healthText, "%.0f%%", healthPercent * 100);
            nvgFontSize(vg, 14.0f);
            nvgFontFaceId(vg, g_nvg_font);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgText(vg, barX + barWidth + 3, barY + barHeight * 0.5f, healthText, NULL);
        }
    }
}

// ==================== NanoVG 画布帧 ====================
void DrawCanvas() {
    if (!vg) return;
    nvgBeginFrame(vg, (float)abs_ScreenX, (float)abs_ScreenY, 1.0f);

    float cx = (float)abs_ScreenX * 0.5f;

    // asuka：顶部中央，白字黑描边
    Vector2A asukaPos;
    asukaPos.X = cx;
    asukaPos.Y = 10.0f;
    DrawOutlinedTextNVG(vg, g_font_agency, "asuka", asukaPos, 60.0f,
                        nvgRGBA(255, 255, 255, 255),   // 白色主字
                        nvgRGBA(0, 0, 0, 255),         // 黑色描边
                        true, 3.0f);

    // ESP
    DrawPlayerNVG(vg);

    // 人数 / 安全：白底小框 + 蓝字，放在 asuka 下方，不再重叠
    char countBuf[16];
    snprintf(countBuf, sizeof(countBuf), "%d", PlayerCount);
    const char* showText = (PlayerCount > 0) ? countBuf : "安全";

    nvgBeginPath(vg);
    nvgRect(vg, cx - 45, 82, 90, 36);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 230));
    nvgFill(vg);

    nvgFontSize(vg, 26.0f);
    nvgFontFaceId(vg, g_nvg_font);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, (PlayerCount > 0) ? nvgRGBA(0, 73, 160, 255) : nvgRGBA(0, 150, 0, 255));
    nvgText(vg, cx, 100, showText, NULL);

    nvgEndFrame(vg);
}

// ==================== UI（ImGui 菜单，保持不变）====================
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