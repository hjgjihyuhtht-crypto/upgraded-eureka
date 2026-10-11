#pragma once
// Base comum para a captura ao vivo (KinectSession) e a reprodução de
// gravações (PlaybackSource): guarda os últimos quadros, mede estatísticas,
// renderiza para exibição, captura quadros e grava.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "calibration.h"
#include "colorize.h"
#include "frame_store.h"
#include "recording.h"

namespace k3d {

class KinectSession;
class PlaybackSource;

// Layout fixo devolvido ao Kotlin (ver NativeKinect.kt).
enum StatIndex : int {
    S_DEPTH_FRAMES = 0,
    S_VIDEO_FRAMES,
    S_DEPTH_FPS,
    S_VIDEO_FPS,
    S_DEPTH_VALID_RATIO,
    S_DEPTH_MIN_MM,
    S_DEPTH_MAX_MM,
    S_DEPTH_CENTER_MM,
    S_VIDEO_MEAN,
    S_SYNC_DELTA_MS,
    S_DEVICE_DEAD,
    S_DEPTH_TS,
    S_VIDEO_TS,
    S_VIDEO_W,
    S_VIDEO_H,
    S_VIDEO_FORMAT,      // 0 nenhum, 1 RGB, 2 IR
    S_REC_ACTIVE,
    S_REC_FRAMES,
    S_REC_DROPPED,
    S_REC_BYTES,
    S_REC_IO_ERROR,
    S_PLAY_POSITION,     // 0..1
    S_PLAY_DURATION_S,
    S_PLAY_TRUNCATED,
    S_COUNT
};

class FrameSource {
public:
    virtual ~FrameSource();
    virtual bool isPlayback() const = 0;
    virtual KinectSession *asLive() { return nullptr; }
    virtual PlaybackSource *asPlayback() { return nullptr; }

    const Calibration &calibration() const { return calib_; }
    FrameStore &store() { return store_; }

    virtual void readStats(double *out) const;

    // Renderização para exibição. Retornam o número de sequência do quadro
    // desenhado, 0 se não há quadro mais novo que afterSeq.
    uint64_t renderDepth(const DepthViewParams &p, uint8_t *rgba, int w, int h, int stride, uint64_t afterSeq);
    uint64_t renderVideo(const VideoViewParams &p, uint8_t *rgba, int w, int h, int stride, uint64_t afterSeq);
    uint64_t renderAligned(const VideoViewParams &p, uint8_t *rgba, int w, int h, int stride, uint64_t afterSeq);

    // Sonda no pixel (x,y) da profundidade: [válido, bruto, mm, X, Y, Z]
    void probe(int x, int y, double out[6]) const;

    // Grava os últimos quadros em 'dir'. Devolve JSON com metadados ou "" (ver err).
    std::string capture(const std::string &dir, DepthFilter filter, std::string &err);

    bool startRecording(const std::string &path, const std::string &json, bool depth, bool video, std::string &err);
    void stopRecording();

protected:
    void onDepth(const uint16_t *raw, int w, int h, uint32_t ts);
    void onVideo(const uint8_t *data, size_t bytes, VideoFormat f, int w, int h, uint32_t ts);
    void resetStats();

    Calibration calib_;
    FrameStore store_;
    std::atomic<bool> dead_{false};

private:
    std::atomic<uint64_t> depthFrames_{0}, videoFrames_{0};
    std::atomic<double> depthFps_{0}, videoFps_{0};
    std::atomic<double> validRatio_{0}, minMm_{0}, maxMm_{0}, centerMm_{0}, videoMean_{0};
    std::atomic<int64_t> lastDepthNs_{0}, lastVideoNs_{0};
    std::atomic<uint32_t> depthTs_{0}, videoTs_{0};
    std::atomic<int> videoW_{0}, videoH_{0}, videoFmt_{0};
    int64_t depthWin_ = 0, videoWin_ = 0;
    uint32_t depthWinCount_ = 0, videoWinCount_ = 0;

    mutable std::mutex recMu_;
    std::unique_ptr<Recorder> rec_;
    std::atomic<bool> recDepth_{false}, recVideo_{false};

    // Buffers reutilizados pela renderização (thread da UI de renderização)
    DepthFrame rDepth_;
    VideoFrame rVideo_;
    std::vector<uint16_t> rMm_, rMmFilt_;
    std::vector<uint8_t> rAligned_;
};

int64_t monotonicNs();

} // namespace k3d
