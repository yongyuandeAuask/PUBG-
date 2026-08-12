// ============================================================
//  VideoFramePlayer v1.0  —  视频帧播放器实现
// ============================================================
#include "VideoFramePlayer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

VideoFramePlayer::VideoFramePlayer() {}
VideoFramePlayer::~VideoFramePlayer() { Shutdown(); }

bool VideoFramePlayer::Init(const char* rawPath, const char* metaPath) {
    if (m_initialized) Shutdown();
    if (!m_uploadFn) return false;  // 必须先 SetUploader()

    // ── 1. 读取元数据 ──
    if (metaPath) {
        FILE* f = fopen(metaPath, "r");
        if (f) {
            fscanf(f, "%d %d %d %d",
                   &m_meta.width, &m_meta.height,
                   &m_meta.fps, &m_meta.frameCount);
            fclose(f);
        }
    }
    m_meta.frameSize = m_meta.width * m_meta.height * 4;

    // ── 2. 打开 raw 帧文件 ──
    int fd = open(rawPath, O_RDONLY);
    if (fd < 0) return false;

    struct stat st;
    size_t fileSize = 0;
    if (fstat(fd, &st) == 0) fileSize = (size_t)st.st_size;

    size_t expected = (size_t)m_meta.frameCount * m_meta.frameSize;
    if (fileSize < expected) {
        close(fd);
        return false;
    }

    // ── 3. 逐帧上传到 GPU ──
    m_textures.resize(m_meta.frameCount);
    unsigned char* buf = (unsigned char*)malloc(m_meta.frameSize);
    if (!buf) { close(fd); return false; }

    int uploaded = 0;
    for (int i = 0; i < m_meta.frameCount; i++) {
        ssize_t offset = (ssize_t)i * m_meta.frameSize;
        ssize_t n = pread(fd, buf, m_meta.frameSize, offset);
        if (n != (ssize_t)m_meta.frameSize) break;

        ImTextureID tex = m_uploadFn(buf, m_meta.width, m_meta.height, 4);
        if (!tex) break;

        m_textures[i] = tex;
        uploaded++;
    }

    free(buf);
    close(fd);

    if (uploaded == 0) return false;
    if (uploaded < m_meta.frameCount) {
        m_meta.frameCount = uploaded;
        m_textures.resize(uploaded);
    }

    m_initialized = true;
    m_startTime = -1.0f;
    return true;
}

void VideoFramePlayer::Update() {
    if (!m_initialized || !enabled) return;
    if (m_startTime < 0.0f) m_startTime = (float)ImGui::GetTime();
}

ImTextureID VideoFramePlayer::GetCurrentTexture() const {
    if (!m_initialized || m_textures.empty()) return (ImTextureID)0;

    float elapsed = (float)ImGui::GetTime() - m_startTime;
    if (elapsed < 0.0f) elapsed = 0.0f;

    float totalDuration = (float)m_meta.frameCount / (float)m_meta.fps;
    float loopTime = fmodf(elapsed, totalDuration);
    int frameIdx = (int)(loopTime * m_meta.fps);
    if (frameIdx < 0) frameIdx = 0;
    if (frameIdx >= m_meta.frameCount) frameIdx = m_meta.frameCount - 1;

    return m_textures[frameIdx];
}

void VideoFramePlayer::Reset() {
    m_startTime = -1.0f;
}

void VideoFramePlayer::Shutdown() {
    m_textures.clear();
    m_initialized = false;
}