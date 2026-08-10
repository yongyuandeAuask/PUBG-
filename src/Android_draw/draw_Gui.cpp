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

        if (DrawIo[2]) {
            char distBuf[16];
            snprintf(distBuf, sizeof(distBuf), "%d", (int)Distance);
            nvgFontSize(vg, 30.0f);
            nvgFontFaceId(vg, g_nvg_font);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgText(vg, Head.X, TOP - 30, distBuf, NULL);
        }

        if (DrawIo[1]) {
            nvgBeginPath(vg);
            nvgRect(vg, X, TOP, W, BOTTOM - TOP);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);
        }

        if (DrawIo[3]) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, px, 130.0f);
            nvgLineTo(vg, r_x, TOP);
            nvgStrokeCo
