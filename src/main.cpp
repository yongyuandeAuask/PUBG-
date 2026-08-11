// src/main.cpp
#include <GLES3/gl3.h>
#include "nanovg.h"
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovg_gl.h"

// 嵌入字体（编译时由 fonts/ 自动生成，【只能在本文件包含一次】）
#include "My_font/AgencyFB-Bold.h"

NVGcontext* vg = nullptr;
int g_nvg_font    = -1;   // 系统中文字体（中文/兜底）
int g_font_agency = -1;   // AgencyFB-Bold（asuka 水印）
int g_font_icons  = -1;   // MaterialIcons（暂不启用）

#include "draw.h"
#include "timer.h"
#include "AndroidImgui.h"
#include "GraphicsManager.h"

timer FPS限制;

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

    static bool flag = true;
    while (flag) {
        drawBegin();
        graphics->NewFrame();

        // 竖屏适配：竖屏时把 DisplaySize 设为 (短边, 长边)，
        // 否则 ImGui 会把悬浮窗限制在竖屏上半部分
        if (::displayInfo.height > ::displayInfo.width) {
            ImGui::GetIO().DisplaySize = ImVec2((float)::abs_ScreenY, (float)::abs_ScreenX);
        }

        Layout_tick_UI(&flag);
        // 注意：ESP 与 asuka 由 OpenGLGraphics::Render() 内的 DrawCanvas() 统一绘制，
        // 这里不再调用任何 NanoVG 绘制函数
        FPS限制.SetFps(FPS);
        FPS限制.AotuFPS();
        graphics->EndFrame();
    }

    if (vg) nvgDeleteGLES3(vg);
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(::window);
    return 0;
}