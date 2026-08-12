// ============================================================
//  VideoFramePlayer v1.0  —  ImGui 视频帧播放器
// ============================================================
//  依赖：imgui.h + 用户提供纹理上传回调
//  用法：预处理视频为 .raw RGBA 帧文件，然后加载播放
//  开源：MIT License
// ============================================================
#pragma once

#include "imgui.h"
#include <vector>
#include <cstdint>
#include <functional>

// ── 视频元数据 ──
struct VideoMeta {
    int width       = 512;
    int height      = 288;
    int fps         = 60;
    int frameCount  = 600;
    int frameSize   = 0;   // width * height * 4 (RGBA)
};

// ── 纹理上传回调 ──
// 参数: raw RGBA 像素数据, 宽, 高, 通道数(4)
// 返回: ImTextureID（在 ImGui 中用于 ImGui::Image 显示）
// 你需要根据自己的图形后端实现此函数（OpenGL/Vulkan）
using UploadTextureFn = std::function<ImTextureID(
    const unsigned char* pixels, int w, int h, int channels)>;

// ── 视频帧循环播放器 ──
class VideoFramePlayer {
public:
    VideoFramePlayer();
    ~VideoFramePlayer();

    // 设置纹理上传函数（必须在 Init() 之前调用）
    void SetUploader(UploadTextureFn fn) { m_uploadFn = fn; }

    // 初始化：读取 raw 帧文件 + 元数据，逐帧上传到 GPU
    // rawPath  : raw RGBA 帧文件路径
    // metaPath : 元数据文本文件（格式: width height fps frameCount）
    bool Init(const char* rawPath, const char* metaPath = nullptr);

    // 每帧调用（可选，内部自动用 ImGui::GetTime() 计时）
    void Update();

    // 获取当前帧纹理 ID
    ImTextureID GetCurrentTexture() const;

    // 重置播放时间（从头开始）
    void Reset();

    // 清理资源（窗口重建/关闭前调用）
    void Shutdown();

    bool        enabled = true;
    const VideoMeta& GetMeta() const { return m_meta; }

private:
    VideoMeta                m_meta;
    std::vector<ImTextureID> m_textures;   // 预上传的全部纹理
    float                    m_startTime = -1.0f;
    bool                     m_initialized = false;
    UploadTextureFn          m_uploadFn;
};