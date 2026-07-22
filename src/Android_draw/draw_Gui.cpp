#include "draw.h"
#include "My_font/zh_Font.h"
#include "读写.h"
#include "UeTool.h"
#include "绘图.h"

// ====== 仅声明 draw.h 中没有的新增变量 ======
static long 类地址 = 0;
static bool 忽略人机 = false;

// ==================== 初始化 ====================
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

// ==================== 更新游戏数据 ====================
void UpdateGameData() {
    if (!初始化) return;

    // 矩阵: libbase+0xf1d5f70 → +0xC0 → +0x590
    Matrix = driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1d5f70) + 0xC0) + 0x590;

    // 世界: libbase+0xf1fb900 → +0x810 → +0x78
    Uworld = driver->read<uint64_t>(driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xf1fb900) + 0x810) + 0x78);

    // ULevel: 世界+0x30
    Uleve = driver->read<uint64_t>(Uworld + 0x30);

    // 数组: ULevel+0xA0
    Arrayaddr = driver->read<uint64_t>(Uleve + 0xA0);

    // 数量: ULevel+0xA8
    Count = driver->read<int>(Uleve + 0xA8);

    // 自身: libbase+0xf1fb900 → +0x810 → +0x78 → +0x38 → +0x78 → +0x30 → +0x28c8
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

    // 类地址: libbase+0xec73720 → +0x110
    类地址 = driver->read<uint64_t>(driver->read<uint64_t>(libbase + 0xec73720) + 0x110);

    // 读取矩阵
    memset(matrix, 0, 16);
    driver->read((uintptr_t)Matrix, matrix, 16 * 4);
}

// ==================== 绘制玩家 ====================
void DrawPlayer(ImDrawList* draw) {
    if (!初始化 || MySelf == 0) return;

    // 自身队伍: 自身+0x998
    int 自己队伍 = driver->read<int>(MySelf + 0x998);

    // 自身坐标: getPtr64(自身+0x208)+0x1c8
    Vector3A Z;
    driver->read((uintptr_t)(driver->read<uint64_t>(MySelf + 0x208) + 0x1c8), &Z, sizeof(Z));

    PlayerCount = 0;

    for (int i = 0; i < Count; i++) {
        // 敌人地址: 数组+i*8
        long int Objaddr = driver->read<uint64_t>(Arrayaddr + 0x8 * i);

        // 地址有效性
        if (Objaddr <= 0xffff || Objaddr == 0 || Objaddr <= 0x10000000 || Objaddr % 4 != 0 || Objaddr >= 0x10000000000) {
            continue;
        }

        // 跳过自己
        if (MySelf == Objaddr) continue;

        // 玩家判断: Obj+0x2b78 == 479.5f
        if (driver->read<float>(Objaddr + 0x2b78) != 479.5f) continue;

        // 类名过滤
        int ClassID = driver->read<int>(Objaddr + 24);
        long FNameEntry = driver->read<uint64_t>(
            driver->read<uint64_t>(类地址 + (ClassID / 0x4000) * 0x8) + (ClassID % 0x4000) * 0x8
        );
        char ClassName[64] = "";
        driver->read((uintptr_t)(FNameEntry + 0xC), ClassName, 64);
        if (strstr(ClassName, "BPPawn_Escape_Raven") != 0 || strstr(ClassName, "BPPawn_Escape_UAV_C") != 0) {
            continue;
        }

        // 状态过滤
        int 状态 = driver->read<int>(Objaddr + 0x1058);
        if (状态 == 1048592 || 状态 == 1048576) continue;

        // 敌人队伍: Obj+0x998
        int 敌人队伍 = driver->read<int>(Objaddr + 0x998);
        if (敌人队伍 == 自己队伍) continue;

        // ====== 忽略人机 ======
        if (忽略人机) {
            int botFlag = driver->read<int>(Objaddr + 0xa59);
            if (敌人队伍 && (botFlag == 16842753 || botFlag == 16843009 || botFlag == 16843008)) {
                continue;
            }
        }

        // 血量: Obj+0xe60 / Obj+0xe64
        float 当前血量 = driver->read<float>(Objaddr + 0xe60);
        float 最大血量 = driver->read<float>(Objaddr + 0xe64);
        if (最大血量 <= 0) continue;

        PlayerCount++;

        // 敌人坐标: getPtr64(Obj+0x208)+0x1c8
        Vector3A D;
        driver->read((uintptr_t)(driver->read<uint64_t>(Objaddr + 0x208) + 0x1c8), &D, sizeof(D));

        // 世界转屏幕
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

        // ==================== 骨骼 ====================
        Vector2A Head, Chest, Pelvis, Left_Shoulder, Right_Shoulder,
                 Left_Elbow, Right_Elbow, Left_Wrist, Right_Wrist,
                 Left_Thigh, Right_Thigh, Left_Knee, Right_Knee,
                 Left_Ankle, Right_Ankle;

        if (DrawIo[4]) {
            // Mesh: Obj+0x510
            long int Mesh = driver->read<uint64_t>(Objaddr + 0x510);
            // Human: Mesh+0x210
            long int human = Mesh + 0x210;
            // Bone: getPtr64(Mesh+0x9a8)+0x30
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

        float bottom;
        if (DrawIo[4]) {
            if (Left_Ankle.Y < Right_Ankle.Y) bottom = Right_Ankle.Y + W / 10;
            else bottom = Left_Ankle.Y + W / 10;
        } else {
            bottom = BOTTOM;
        }

        // ==================== 绘制 ====================

        // 距离
        if (DrawIo[2]) {
            char distBuf[16];
            snprintf(distBuf, sizeof(distBuf), "%d", (int)Distance);
            auto textSize = ImGui::CalcTextSize(distBuf, NULL, 25);
            draw->AddText(NULL, 30, ImVec2(Head.X - (textSize.x / 2), TOP - 30), ImColor(255, 255, 255), distBuf);
        }

        // 方框
        if (DrawIo[1]) {
            draw->AddRect(ImVec2(X, TOP), ImVec2(X + W, BOTTOM), BoxColor, 0, 0, 1.5f);
        }

        // 射线
        if (DrawIo[3]) {
            draw->AddLine(ImVec2(px, 130), ImVec2(r_x, TOP), ImColor(255, 0, 0), 1.0f);
        }

        // 敌人地址
        if (DrawIo[7]) {
            char buf[32];
            sprintf(buf, "0x%lx", Objaddr);
            draw->AddText(ImVec2(MIDDLE, BOTTOM), ImColor(239, 241, 245), buf);
        }

        // 骨骼
        if (DrawIo[4]) {
            draw->AddCircle(ImVec2(Head.X, Head.Y), W / 5, BoneColor, 0);
            draw->AddLine(ImVec2(Head.X, Head.Y), ImVec2(Chest.X, Chest.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Chest.X, Chest.Y), ImVec2(Pelvis.X, Pelvis.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Chest.X, Chest.Y), ImVec2(Left_Shoulder.X, Left_Shoulder.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Chest.X, Chest.Y), ImVec2(Right_Shoulder.X, Right_Shoulder.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Left_Shoulder.X, Left_Shoulder.Y), ImVec2(Left_Elbow.X, Left_Elbow.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Right_Shoulder.X, Right_Shoulder.Y), ImVec2(Right_Elbow.X, Right_Elbow.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Left_Elbow.X, Left_Elbow.Y), ImVec2(Left_Wrist.X, Left_Wrist.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Right_Elbow.X, Right_Elbow.Y), ImVec2(Right_Wrist.X, Right_Wrist.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Pelvis.X, Pelvis.Y), ImVec2(Left_Thigh.X, Left_Thigh.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Pelvis.X, Pelvis.Y), ImVec2(Right_Thigh.X, Right_Thigh.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Left_Thigh.X, Left_Thigh.Y), ImVec2(Left_Knee.X, Left_Knee.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Right_Thigh.X, Right_Thigh.Y), ImVec2(Right_Knee.X, Right_Knee.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Left_Knee.X, Left_Knee.Y), ImVec2(Left_Ankle.X, Left_Ankle.Y), BoneColor, 2.5f);
            draw->AddLine(ImVec2(Right_Knee.X, Right_Knee.Y), ImVec2(Right_Ankle.X, Right_Ankle.Y), BoneColor, 2.5f);
        }

        // 名字: getPtr64(Obj+0x960)
        if (DrawIo[5]) {
            getUTF8(PlayerName, driver->read<uint64_t>(Objaddr + 0x960));
            auto textSize = ImGui::CalcTextSize(PlayerName, NULL, 28);
            draw->AddText(NULL, 28, ImVec2(MIDDLE - 45, TOP - 55), ImColor(248, 248, 255), PlayerName);
        }

        // 血量条
        if (DrawIo[6]) {
            float healthPercent = 最大血量 > 0 ? (当前血量 / 最大血量) : 0.0f;
            float barX = X;
            float barY = TOP - 16 - 8;
            float barWidth = W;
            float barHeight = 8;
            draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight), ImColor(60, 60, 60));
            float fillWidth = barWidth * healthPercent;
            draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillWidth, barY + barHeight), ImColor(10, 240, 10));
            char healthText[16];
            sprintf(healthText, "%.0f%%", healthPercent * 100);
            float textX = barX + barWidth + 3;
            float textY = barY + (barHeight - ImGui::CalcTextSize(healthText).y) / 2;
            draw->AddText(ImVec2(textX, textY), ImColor(255, 255, 255), healthText);
        }
    }

    // 人数
    if (PlayerCount > 0) {
        char countBuf[16];
        snprintf(countBuf, sizeof(countBuf), "%d", PlayerCount);
        draw->AddText(NULL, 50.0f, ImVec2(px, 50), IM_COL32(255, 0, 0, 255), countBuf);
    } else {
        draw->AddText(NULL, 50.0f, ImVec2(px, 50), IM_COL32(0, 255, 0, 255), "安全");
    }
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