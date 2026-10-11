#pragma once
// Último quadro de cada fluxo, com cópia sob mutex. Os produtores (callback do
// libfreenect ou player de arquivo) escrevem; renderização, captura e sonda leem.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace k3d {

enum class VideoFormat : uint8_t { None = 0, Rgb = 1, Ir8 = 2 };

struct DepthFrame {
    std::vector<uint16_t> raw;   // 11 bits, 2047 = sem medida
    int width = 0, height = 0;
    uint32_t deviceTs = 0;
    int64_t hostNs = 0;
    uint64_t seq = 0;
};

struct VideoFrame {
    std::vector<uint8_t> data;   // RGB888 ou IR 8 bits
    VideoFormat format = VideoFormat::None;
    int width = 0, height = 0;
    uint32_t deviceTs = 0;
    int64_t hostNs = 0;
    uint64_t seq = 0;
};

class FrameStore {
public:
    void putDepth(const uint16_t *raw, int w, int h, uint32_t ts, int64_t hostNs) {
        std::lock_guard<std::mutex> lk(mu_);
        depth_.raw.assign(raw, raw + static_cast<size_t>(w) * h);
        depth_.width = w; depth_.height = h;
        depth_.deviceTs = ts; depth_.hostNs = hostNs;
        depth_.seq = ++depthSeq_;
    }
    void putVideo(const uint8_t *data, size_t bytes, VideoFormat f, int w, int h, uint32_t ts, int64_t hostNs) {
        std::lock_guard<std::mutex> lk(mu_);
        video_.data.assign(data, data + bytes);
        video_.format = f; video_.width = w; video_.height = h;
        video_.deviceTs = ts; video_.hostNs = hostNs;
        video_.seq = ++videoSeq_;
    }
    // Copiam o quadro se for mais novo que 'afterSeq'. Retornam false se não houver novo.
    bool copyDepth(DepthFrame &out, uint64_t afterSeq = 0) const {
        std::lock_guard<std::mutex> lk(mu_);
        if (depth_.seq == 0 || depth_.seq <= afterSeq) return false;
        out = depth_;
        return true;
    }
    bool copyVideo(VideoFrame &out, uint64_t afterSeq = 0) const {
        std::lock_guard<std::mutex> lk(mu_);
        if (video_.seq == 0 || video_.seq <= afterSeq) return false;
        out = video_;
        return true;
    }
    uint64_t depthSeq() const { std::lock_guard<std::mutex> lk(mu_); return depth_.seq; }
    uint64_t videoSeq() const { std::lock_guard<std::mutex> lk(mu_); return video_.seq; }
    void clearVideo() { std::lock_guard<std::mutex> lk(mu_); video_ = VideoFrame{}; }
    void clearDepth() { std::lock_guard<std::mutex> lk(mu_); depth_ = DepthFrame{}; }

private:
    mutable std::mutex mu_;
    DepthFrame depth_;
    VideoFrame video_;
    uint64_t depthSeq_ = 0, videoSeq_ = 0;
};

} // namespace k3d
