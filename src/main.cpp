// 【关键】Android 环境下必须用 GLES3 宏，且必须先包含 gl3.h
#include <GLES3/gl3.h>
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg.h"

// 定义 NanoVG 全局上下文
NVGcontext* vg = nullptr;
int g_nvg_font = -1; // 字体 ID

#include "draw.h"    
#include "timer.h"
#include "AndroidImgui.h"     
#include "GraphicsManager.h" 

timer FPS限制;

// 声明底层 OpenGLGraphics.cpp 里的回调指针
extern void (*g_nanovg_render_callback)();

// 【新增】定义画红字的专属函数
void DrawMyRedText() {
    if (!vg || g_nvg_font == -1) return;
    
    nvgBeginFrame(vg, ::native_window_screen_x, ::native_window_screen_y, 1.0f);
    
    // 设置 80号字，居中对齐
    nvgFontSize(vg, 80.0f);
    nvgFontFaceId(vg, g_nvg_font);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    
    // 画纯红色的 "asuka"
    nvgFillColor(vg, nvgRGBA(255, 0, 0, 255));
    nvgText(vg, ::native_window_screen_x / 2.0f, ::native_window_screen_y / 2.0f, "asuka", NULL);
    
    nvgEndFrame(vg);
}

int main(int argc, char *argv[]) {
    // 【关键】切换为 OPENGL，NanoVG 不支持 Vulkan
    ::graphics = GraphicsManager::getGraphicsInterface(GraphicsManager::OPENGL);

    ::screen_config(); 
    ::native_window_screen_x = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::native_window_screen_y = (::displayInfo.height < ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::abs_ScreenX = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::abs_ScreenY = (::displayInfo.height < ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);

    ::window = android::ANativeWindowCreator::Create("test", native_window_screen_x, native_window_screen_y, permeate_record);
    graphics->Init_Render(::window, native_window_screen_x, native_window_screen_y);
    
    // 【核心】初始化 NanoVG 引擎
    vg = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    
    // 【核心】加载手机系统字体 (尝试多个常见路径，防止不同手机系统路径不同)
    g_nvg_font = nvgCreateFont(vg, "default", "/system/fonts/Roboto-Regular.ttf");
    if (g_nvg_font == -1) g_nvg_font = nvgCreateFont(vg, "default", "/system/fonts/DroidSans.ttf");
    if (g_nvg_font == -1) g_nvg_font = nvgCreateFont(vg, "default", "/system/fonts/NotoSansCJK-Regular.ttc");
    
    // 【核心】把画红字的函数注册给底层，防止被 glClear 擦除！
    g_nanovg_render_callback = DrawMyRedText;

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
        DrawPlayer(ImGui::GetForegroundDrawList());        
        
        FPS限制.SetFps(FPS);
        FPS限制.AotuFPS();
        graphics->EndFrame(); 
    }
    
    // 退出清理
    if (vg) nvgDeleteGLES3(vg);
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(::window);
    return 0;
}
