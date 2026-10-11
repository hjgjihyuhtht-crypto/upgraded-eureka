#include "playback_source.h"

#include <algorithm>
#include <chrono>

namespace k3d {

bool PlaybackSource::open(const std::string &path, std::string &err) {
    close();
    if (!reader_.open(path, err)) return false;
    const auto &cal = reader_.calibration();
    if (!cal.empty()) calib_ = Calibration::deserialize(cal.data(), cal.size());
    const auto &idx = reader_.index();
    durationS_ = (idx.back().h.hostNs - idx.front().h.hostNs) / 1e9;
    resetStats();
    running_ = true;
    th_ = std::thread(&PlaybackSource::run, this);
    return true;
}

void PlaybackSource::close() {
    running_ = false;
    if (th_.joinable()) th_.join();
}

void PlaybackSource::run() {
    using clock = std::chrono::steady_clock;
    const auto &idx = reader_.index();
    const int64_t t0 = idx.front().h.hostNs;
    const int64_t span = idx.back().h.hostNs - t0;
    std::vector<uint8_t> buf;
    size_t i = 0;
    // Relógio de mídia: posição em ns de gravação, avançada pelo tempo real × velocidade.
    double mediaNs = 0;
    auto last = clock::now();

    while (running_) {
        const double seek = seekTo_.exchange(-1.0);
        if (seek >= 0) {
            mediaNs = seek * span;
            i = 0;
            while (i < idx.size() && idx[i].h.hostNs - t0 < mediaNs) ++i;
        }
        const auto now = clock::now();
        const double dt = std::chrono::duration<double, std::nano>(now - last).count();
        last = now;
        if (!paused_) mediaNs += dt * speed_.load();

        if (i >= idx.size()) {
            if (!loop_) { paused_ = true; mediaNs = static_cast<double>(span); }
            else { i = 0; mediaNs = 0; }
            continue;
        }
        // Entrega todos os registros cujo instante já passou.
        bool delivered = false;
        while (i < idx.size() && idx[i].h.hostNs - t0 <= mediaNs) {
            const auto &e = idx[i++];
            if (!reader_.read(e, buf)) continue;
            switch (e.h.type) {
                case RecordType::Depth:
                    if (buf.size() == static_cast<size_t>(e.h.width) * e.h.height * 2)
                        onDepth(reinterpret_cast<const uint16_t *>(buf.data()), e.h.width, e.h.height, e.h.deviceTs);
                    break;
                case RecordType::Rgb:
                    onVideo(buf.data(), buf.size(), VideoFormat::Rgb, e.h.width, e.h.height, e.h.deviceTs);
                    break;
                case RecordType::Ir8:
                    onVideo(buf.data(), buf.size(), VideoFormat::Ir8, e.h.width, e.h.height, e.h.deviceTs);
                    break;
            }
            delivered = true;
        }
        position_ = span > 0 ? std::min(1.0, mediaNs / span) : 1.0;
        if (!delivered) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void PlaybackSource::readStats(double *out) const {
    FrameSource::readStats(out);
    out[S_PLAY_POSITION] = position_.load();
    out[S_PLAY_DURATION_S] = durationS_;
    out[S_PLAY_TRUNCATED] = reader_.truncated() ? 1 : 0;
}

} // namespace k3d
