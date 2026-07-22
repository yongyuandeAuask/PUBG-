
bool permeate_record = false;
bool permeate_record_ini = false;
struct Last_ImRect LastCoordinate = {0, 0, 0, 0};
float FPS = 120.0f;


std::unique_ptr<AndroidImgui> graphics;
ANativeWindow *window = NULL; 
android::ANativeWindowCreator::DisplayInfo displayInfo;
ImGuiWindow *g_window = NULL;
int abs_ScreenX = 0, abs_ScreenY = 0;
int native_window_screen_x = 0, native_window_screen_y = 0;


ImFont* zh_font = NULL;

long int libbase, Uworld, Arrayaddr, Matrix, MySelf, Uleve, Gname;
int Count, PlayerCount = 0;
char PlayerName[64];
bool DrawIo[50];
bool 初始化 = false;  


ImColor BoxColor = {1.0f, 0.0f, 0.0f, 1.0f};
ImColor BoneColor = ImColor(255, 0, 0, 255);
ImColor BotBoxColor = ImColor(255, 255, 255, 255);
ImColor LineColor = ImColor(255, 0, 0, 255);
ImColor BotLineColor = ImColor(255, 255, 255, 255);




void 绘制字体描边(float size, int x, int y, ImVec4 color, const char* str) {
    ImGui::GetBackgroundDrawList()->AddText(NULL, size, ImVec2(x + 1, y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size, ImVec2(x - 0.1f, y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size, ImVec2(x, y + 1), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size, ImVec2(x, y - 1), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size, ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(color), str);
}


bool M_Android_LoadFont(float SizePixels) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.SizePixels = SizePixels;
    config.OversampleH = 1;
    ::zh_font = io.Fonts->AddFontFromMemoryTTF((void*)OPPOSans_H, OPPOSans_H_size, 0.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
    io.Fonts->AddFontDefault();
    return zh_font != nullptr;
}

void init_My_drawdata() {
    M_Android_LoadFont(25.0f);
    ImGui::GetStyle().ScaleAllSizes(3.0f);
}

void screen_config() {
    ::displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
}

void drawBegin() {
    if (::permeate_record_ini) {
        LastCoordinate.Pos_x = ::g_window->Pos.x;
        LastCoordinate.Pos_y = ::g_window->Pos.y;
        LastCoordinate.Size_x = ::g_window->Size.x;
        LastCoordinate.Size_y = ::g_window->Size.y;
        graphics->Shutdown();
        android::ANativeWindowCreator::Destroy(::window);
        ::window = android::ANativeWindowCreator::Create("ImGui", native_window_screen_x, native_window_screen_y, permeate_record);
        graphics->Init_Render(::window, native_window_screen_x, native_window_screen_y);
        ::init_My_drawdata();
    }
    static uint32_t orientation = -1;
    screen_config();
    if (orientation != displayInfo.orientation) {
        orientation = displayInfo.orientation;
        Touch::setOrientation(displayInfo.orientation);
        if (g_window != NULL) {
            g_window->Pos.x = 100;
            g_window->Pos.y = 125;
        }
    }
    px = abs_ScreenX / 2;
    py = abs_ScreenY / 2;
}
