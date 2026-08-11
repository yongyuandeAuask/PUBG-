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
// 对应 UE: K2_DrawText(Canvas, Font, Text, Pos, Color, ..., isCenter, ..., OutlineColor)
// isCenter : true 时 pos.X 为水平居中锚点，false 为左对齐锚点
// outlineWidth : 描边宽度（像素）
void DrawOutlinedTextNVG(NVGcontext* vg, int fontId, const char* text,
                         Vector2A pos, float fontSize,
                         NVGcolor color, NVGcolor outlineColor,
                         bool isCenter = false, float outlineWidth = 2.0f)
{
    if (!vg || !text || fontId < 0) return;

    nvgFontSize(vg, fontSize);
    nvgFontFaceId(vg, fontId);
    nvgTextAlign(vg, (isCenter ? NVG_ALIGN_CENTER : NVG_ALIGN_LEFT) | NVG_ALIGN_TOP);

    // 描边：8 方向偏移各画一遍
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

    // 主文字
    nvgFillColor(vg, color);
    nvgText(vg, pos.X, pos.Y, text, NULL);
}

// ==================== 绘制玩家（NanoVG 版）====================
void DrawPlayerNVG(NVGcontext* vg) {
    if (!初始化 || MySelf == 0 || vg == nullptr) return;

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
        long FNameEntry = driver->read<uint64_t>(driver->read<uint64_t>(类地址 + (ClassID / 0x4000) * 0x8) + (ClassID % 0x4000) * 0x8);
        char ClassName[64] = "";
        driver->read((uintptr_t)(FNameEntry + 0xC), ClassName, 64);
        if (strstr(ClassName, "BPPawn_Escape_Raven") != 0 || strstr(ClassName, "BPPawn_Escape_UAV_C") != 0) continue;

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

        Vector2A Head, Chest, Pelvis, Left_Shoulder, Right_Shoulder,
                 Left_Elbow, Right_Elbow, Left_Wrist, Right_Wrist,
                 Left_Thigh, Right_Thigh, Left_Knee, Right_Knee,
                 Left_Ankle, Right_Ankle;

        if (DrawIo[4]) {
            long int Mesh = driver->read<uint64_t>(Objaddr + 0x510);
            long int human = Mesh + 0x210;
            long int Bone = driver->read<uint64_t>(Mesh + 0x9a8) + 0x30;

            FTransform meshtrans = getBone(human);
            FMatrix c2wMatrix = TransformToMatrix(meshtrans);

            FTransform headtrans = getBone(Bone + 9 * 48);
            FMatrix boneMatrix = TransformToMatrix(headtrans);
            Vector3A relLocation = MarixToVector(MatrixMulti(boneMatrix, c2wMatrix));
            Head = WorldToScreen(relLocation, matrix, camera);

            FTransform chesttrans = getBone(Bone + 5 * 48);
            FMatrix boneMatrix1 = TransformToMatrix(chesttrans);
            Vector3A relLocation1 = MarixToVector(MatrixMulti(boneMatrix1, c2wMatrix));
            Chest = WorldToScreen(relLocation1, matrix, camera);

            FTransform pelvistrans = getBone(Bone + 2 * 48);
            FMatrix boneMatrix2 = TransformToMatrix(pelvistrans);
            Vector3A LrelLocation1 = MarixToVector(MatrixMulti(boneMatrix2, c2wMatrix));
            Pelvis = WorldToScreen(LrelLocation1, matrix, camera);

            FTransform lshtrans = getBone(Bone + 21 * 48);
            FMatrix boneMatrix3 = TransformToMatrix(lshtrans);
            Vector3A relLocation2 = MarixToVector(MatrixMulti(boneMatrix3, c2wMatrix));
            Left_Shoulder = WorldToScreen(relLocation2, matrix, camera);

            FTransform rshtrans = getBone(Bone + 44 * 48);
            FMatrix boneMatrix4 = TransformToMatrix(rshtrans);
            Vector3A relLocation3 = MarixToVector(MatrixMulti(boneMatrix4, c2wMatrix));
            Right_Shoulder = WorldToScreen(relLocation3, matrix, camera);

            FTransform lelbtrans = getBone(Bone + 22 * 48);
            FMatrix boneMatrix5 = TransformToMatrix(lelbtrans);
            Vector3A relLocation4 = MarixToVector(MatrixMulti(boneMatrix5, c2wMatrix));
            Left_Elbow = WorldToScreen(relLocation4, matrix, camera);

            FTransform relbtrans = getBone(Bone + 45 * 48);
            FMatrix boneMatrix6 = TransformToMatrix(relbtrans);
            Vector3A relLocation5 = MarixToVector(MatrixMulti(boneMatrix6, c2wMatrix));
            Right_Elbow = WorldToScreen(relLocation5, matrix, camera);

            FTransform lwtrans = getBone(Bone + 23 * 48);
            FMatrix boneMatrix7 = TransformToMatrix(lwtrans);
            Vector3A relLocation6 = MarixToVector(MatrixMulti(boneMatrix7, c2wMatrix));
            Left_Wrist = WorldToScreen(relLocation6, matrix, camera);

            FTransform rwtrans = getBone(Bone + 46 * 48);
            FMatrix boneMatrix8 = TransformToMatrix(rwtrans);
            Vector3A relLocation7 = MarixToVector(MatrixMulti(boneMatrix8, c2wMatrix));
            Right_Wrist = WorldToScreen(relLocation7, matrix, camera);

            FTransform Llshtrans = getBone(Bone + 68 * 48);
            FMatrix boneMatrix9 = TransformToMatrix(Llshtrans);
            Vector3A LrelLocation2 = MarixToVector(MatrixMulti(boneMatrix9, c2wMatrix));
            Left_Thigh = WorldToScreen(LrelLocation2, matrix, camera);

            FTransform Lrshtrans = getBone(Bone + 72 * 48);
            FMatrix boneMatrix10 = TransformToMatrix(Lrshtrans);
            Vector3A LrelLocation3 = MarixToVector(MatrixMulti(boneMatrix10, c2wMatrix));
            Right_Thigh = WorldToScreen(LrelLocation3, matrix, camera);

            FTransform Llelbtrans = getBone(Bone + 69 * 48);
            FMatrix boneMatrix11 = TransformToMatrix(Llelbtrans);
            Vector3A LrelLocation4 = MarixToVector(MatrixMulti(boneMatrix11, c2wMatrix));
            Left_Knee = WorldToScreen(LrelLocation4, matrix, camera);

            FTransform Lrelbtrans = getBone(Bone + 73 * 48);
            FMatrix boneMatrix12 = TransformToMatrix(Lrelbtrans);
            Vector3A LrelLocation5 = MarixToVector(MatrixMulti(boneMatrix12, c2wMatrix));
            Right_Knee = WorldToScreen(LrelLocation5, matrix, camera);

            FTransform Llwtrans = getBone(Bone + 70 * 48);
            FMatrix boneMatrix13 = TransformToMatrix(Llwtrans);
            Vector3A LrelLocation6 = MarixToVector(MatrixMulti(boneMatrix13, c2wMatrix));
            Left_Ankle = WorldToScreen(LrelLocation6, matrix, camera);

            FTransform Lrwtrans = getBone(Bone + 74 * 48);
            FMatrix boneMatrix14 = TransformToMatrix(Lrwtrans);
            Vector3A LrelLocation7 = MarixToVector(MatrixMulti(boneMatrix14, c2wMatrix));
            Right_Ankle = WorldToScreen(LrelLocation7, matrix, camera);
        }

        float bottom = DrawIo[4] ? ((Left_Ankle.Y < Right_Ankle.Y) ? Right_Ankle.Y + W / 10 : Left_Ankle.Y + W / 10) : BOTTOM;

        // 距离
        if (DrawIo[2]) {
            char distBuf[16];
            snprintf(distBuf, sizeof(distBuf), "%d", (int)Distance);
            DrawOutlinedTextNVG(vg, g_nvg_font, distBuf, {Head.X, TOP - 30}, 30.0f,
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
        if (DrawIo[4]) {
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

    // 人数 / 安全
    Vector2A countPos;
    countPos.X = px;
    countPos.Y = 50;
    if (PlayerCount > 0) {
        char countBuf[16];
        snprintf(countBuf, sizeof(countBuf), "%d", PlayerCount);
        DrawOutlinedTextNVG(vg, g_nvg_font, countBuf, countPos, 50.0f,
                            nvgRGBA(255, 0, 0, 255), nvgRGBA(0, 0, 0, 255), false, 2.0f);
    } else {
        DrawOutlinedTextNVG(vg, g_nvg_font, "安全", countPos, 50.0f,
                            nvgRGBA(0, 255, 0, 255), nvgRGBA(0, 0, 0, 255), false, 2.0f);
    }
}

// ==================== NanoVG 画布帧 ====================
// 由 OpenGLGraphics::Render() 在 ImGui 渲染之后、eglSwapBuffers 之前调用，
// 保证 NanoVG 内容不会被 glClear 清掉
void DrawCanvas() {
    if (!vg) return;
    nvgBeginFrame(vg, (float)abs_ScreenX, (float)abs_ScreenY, 1.0f);

    // asuka：屏幕顶部中央，AgencyFB-Bold，红字黑描边
    Vector2A asukaPos;
    asukaPos.X = (float)abs_ScreenX * 0.5f;
    asukaPos.Y = 20.0f;
    DrawOutlinedTextNVG(vg, g_font_agency, "asuka", asukaPos, 64.0f,
                        nvgRGBA(255, 0, 0, 255),     // Color
                        nvgRGBA(0, 0, 0, 255),       // OutlineColor
                        true,                        // isCenter
                        3.0f);                       // 描边宽度

    // ESP 绘制
    DrawPlayerNVG(vg);

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