#pragma once
// Reprodução de gravações .k3drec. É uma SIMULAÇÃO: os dados vêm de arquivo,
// não do sensor; a interface deve sempre identificá-la assim.
#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "frame_source.h"

namespace k3d {

class PlaybackSource : public FrameSource {
public:
    ~PlaybackSource() override { close(); }
    bool isPlayback() const override { return true; }
    PlaybackSource *asPlayback() override { return this; }

    bool open(const std::string &path, std::string &err);
    void close();

    void setSpeed(double s) { speed_ = s < 0.1 ? 0.1 : (s > 4.0 ? 4.0 : s); }
    void setPaused(bool p) { paused_ = p; }
    void setLoop(bool l) { loop_ = l; }
    void seek(double fraction) { seekTo_ = fraction < 0 ? 0 : (fraction > 1 ? 1 : fraction); }
    const std::string &metadataJson() const { return reader_.json(); }

    void readStats(double *out) const override;

private:
    void run();

    RecordingReader reader_;
    std::thread th_;
    std::atomic<bool> running_{false}, paused_{false}, loop_{true};
    std::atomic<double> speed_{1.0}, seekTo_{-1.0}, position_{0.0};
    double durationS_ = 0;
};

} // namespace k3d
