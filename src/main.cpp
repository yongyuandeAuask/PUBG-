// src/main.cpp
#include <GLES3/gl3.h>
#include "nanovg.h"
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovg_gl.h"

// 嵌入字体（编译时由 fonts/ 自动生成，【只能在本文件包含一次】）
#include "My_font/AgencyFB-Bold.h"
#include "My_font/MaterialIcons-Regular.h"
#include <iostream>
#include <string>
#include <thread>
#include <cstdio>
#include <unistd.h>
#include <signal.h>

NVGcontext* vg = nullptr;
int g_nvg_font    = -1;   // 系统中文字体（中文/兜底）
int g_font_agency = -1;   // AgencyFB-Bold（asuka 水印）
int g_font_icons  = -1;   // MaterialIcons（暂不启用）

#include "draw.h"
#include "timer.h"
#include "AndroidImgui.h"
#include "GraphicsManager.h"

void DrawCanvas();

timer FPS限制;

// ==================== 简易控制台：输入 1 强制结束进程 ====================
// 从终端启动本程序时，输入 1 回车立即强杀，
// 触摸 grab / 悬浮窗随进程死亡自动释放
static void ConsoleThread() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "1") {
            printf("收到退出指令，进程强制结束\n");
            fflush(stdout);
            kill(getpid(), SIGKILL);
        }
    }
}

int main(int argc, char *argv[]) {
    ::graphics = GraphicsManager::getGraphicsInterface(GraphicsManager::OPENGL);

    ::screen_config();
    ::native_window_screen_x = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::native_window_screen_y = (::displayInfo.height < ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::abs_ScreenX = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::abs_ScreenY = (::displayInfo.height < ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);

    ::window = android::ANativeWindowCreator::Create("test", native_window_screen_x, native_window_screen_y, permeate_record);
    graphics->Init_Render(::window, native_window_screen_x, native_window_screen_y);

    // 初始化 NanoVG
    vg = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);

    // 嵌入字体（const 数组需要强制转换）
    g_font_agency = nvgCreateFontMem(vg, "agency", (unsigned char*)AgencyFB_Bold_ttf, (int)AgencyFB_Bold_ttf_len, 0);
    g_font_icons = nvgCreateFontMem(vg, "icons", MaterialIcons_Regular_otf, (int)MaterialIcons_Regular_otf_len, 0);
    // 中文字体走系统（中文太大不嵌入）
    g_nvg_font = nvgCreateFont(vg, "zh", "/system/fonts/NotoSansCJK-Regular.ttc");
    if (g_nvg_font == -1) g_nvg_font = nvgCreateFont(vg, "zh", "/system/fonts/Roboto-Regular.ttf");
    if (g_nvg_font == -1) g_nvg_font = nvgCreateFont(vg, "zh", "/system/fonts/DroidSans.ttf");

    Touch::Init({(float)::abs_ScreenX, (float)::abs_ScreenY}, false);
    Touch::setOrientation(displayInfo.orientation);

    FPS限制.SetFps(FPS);
    FPS限制.AotuFPS_init();
    FPS限制.setAffinity();

    ::init_My_drawdata();

    // 启动控制台线程
    printf("控制台已启动：输入 1 并回车可强制结束本进程\n");
    std::thread(ConsoleThread).detach();

    static bool flag = true;
    while (flag) {
        drawBegin();
        graphics->NewFrame();   // 内部先 glClear 清屏
        DrawCanvas();           // NanoVG（asuka + ESP）：清屏之后、ImGui 之前画
        Layout_tick_UI(&flag);  // ImGui 菜单叠在最上层
        FPS限制.SetFps(FPS);
        FPS限制.AotuFPS();
        graphics->EndFrame();   // 只负责 ImGui 渲染 + swap，不再清屏
    }

    if (vg) nvgDeleteGLES3(vg);
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(::window);
    return 0;
}
