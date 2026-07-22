#include "draw.h"    
#include "timer.h"
#include "AndroidImgui.h"     
#include "GraphicsManager.h" 

timer FPS限制;

int main(int argc, char *argv[]) {
    ::graphics = GraphicsManager::getGraphicsInterface(GraphicsManager::VULKAN);

    ::screen_config(); 
    ::native_window_screen_x = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::native_window_screen_y = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::abs_ScreenX = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::abs_ScreenY = (::displayInfo.height < ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);

    ::window = android::ANativeWindowCreator::Create("test", native_window_screen_x, native_window_screen_y, permeate_record);
    graphics->Init_Render(::window, native_window_screen_x, native_window_screen_y);
    
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
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(::window);
    return 0;
}
