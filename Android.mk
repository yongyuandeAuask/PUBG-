LOCAL_PATH := $(call my-dir)

# ============ paradise 静态库 ============
include $(CLEAR_VARS)
LOCAL_MODULE := paradise_api
LOCAL_SRC_FILES := libs/arm64-v8a/libparadise_api.a
include $(PREBUILT_STATIC_LIBRARY)

# ============ 主模块 ============
include $(CLEAR_VARS)
LOCAL_MODULE := start.sh
LOCAL_CFLAGS := -std=c17
LOCAL_CFLAGS += -fvisibility=hidden -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables
LOCAL_CPPFLAGS := -std=c++17
LOCAL_CPPFLAGS += -fvisibility=hidden -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables
LOCAL_CFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES
LOCAL_CPPFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES -DIMGUI_DISABLE_DEBUG_TOOLS -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DISABLE_DEMO_WINDOWS
LOCAL_LDFLAGS := -Wl,--gc-sections -Wl,--strip-all -Wl,--as-needed -Wl,--no-undefined -Wl,-O3
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_draw
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_Graphics
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_my_imgui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_touch
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/My_Utils
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/backends
LOCAL_SRC_FILES := src/main.cpp
LOCAL_SRC_FILES += src/Android_draw/nanovg.c
LOCAL_SRC_FILES += src/Android_draw/draw_Gui.cpp
LOCAL_SRC_FILES += src/Android_touch/TouchHelperA.cpp
LOCAL_SRC_FILES += src/Android_Graphics/GraphicsManager.cpp
LOCAL_SRC_FILES += src/Android_Graphics/OpenGLGraphics.cpp
LOCAL_SRC_FILES += src/Android_Graphics/VulkanGraphics.cpp
LOCAL_SRC_FILES += src/Android_Graphics/vulkan_wrapper.cpp
LOCAL_SRC_FILES += src/Android_my_imgui/AndroidImgui.cpp
LOCAL_SRC_FILES += src/Android_my_imgui/my_imgui.cpp
LOCAL_SRC_FILES += src/Android_my_imgui/my_imgui_impl_android.cpp
LOCAL_SRC_FILES += src/ImGui/imgui.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_demo.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_draw.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_tables.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_widgets.cpp
LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_android.cpp
LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_opengl3.cpp
LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_vulkan.cpp
LOCAL_SRC_FILES += src/My_Utils/stb_image.cpp

# ====== 链接 paradise ======
LOCAL_STATIC_LIBRARIES := paradise_api

LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3
include $(BUILD_EXECUTABLE)
