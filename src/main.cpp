// src/main.cpp
#include <GLES3/gl3.h>
#include "nanovg.h"
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovg_gl.h"

// --- 关键修复：正确包含字体头文件 ---
#include "My_font/AgencyFB_Bold.h"  // ✅ 正确路径
// #include "My_font/MaterialIcons_Regular.h"  // ❌ 注释掉（不需要）

NVGcontext* vg = nullptr;
int g_nvg_font    = -1;   // 系统中文字体（中文/兜底）
int g_font_agency = -1;   // AgencyFB-Bold（顶部 asuka）
int g_font_icons  = -1;   // MaterialIcons（名字/距离）

#include "draw.h"    
#include "timer.h"
#include "AndroidImgui.h"     
#include "GraphicsManager.h" 

// --- 新增函数声明 ---
void DrawPlayerNVG(NVGcontext* vg);

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
    
    // --- 修复字体加载 ---
    g_font_agency = nvgCreateFontMem(vg, "agency", AgencyFB_Bold_ttf, (int)AgencyFB_Bold_ttf_len, 0);
    // g_font_icons = nvgCreateFontMem(vg, "icons", MaterialIcons_Regular_otf, (int)MaterialIcons_Regular_otf_len, 0);
    
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
        Layout_tick_UI(&flag);
        DrawPlayerNVG(vg);
        FPS限制.SetFps(FPS);
        FPS限制.AotuFPS();
        graphics->EndFrame(); 
    }
    
    if (vg) nvgDeleteGLES3(vg);
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(::window);
    return 0;
}
