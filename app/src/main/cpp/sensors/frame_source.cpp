#include "frame_source.h"
#include "portable_io.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace k3d {

int64_t monotonicNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

namespace {
void updateFps(int64_t now, int64_t &start, uint32_t &count, std::atomic<double> &fps) {
    if (start == 0) { start = now; count = 0; }
    ++count;
    if (now - start >= 1'000'000'000LL) {
        fps.store(count * 1e9 / static_cast<double>(now - start));
        start = now;
        count = 0;
    }
}

// PGM/PPM binários: formatos simples e abertos, lidos por GIMP, ImageMagick, OpenCV, numpy.
bool writePgm16(const std::string &path, const uint16_t *v, int w, int h, int maxval) {
    FILE *f = openFileUtf8(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P5\n%d %d\n%d\n", w, h, maxval);
    std::vector<uint8_t> be(static_cast<size_t>(w) * h * 2);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {  // PGM 16 bits é big-endian
        be[i * 2] = static_cast<uint8_t>(v[i] >> 8);
        be[i * 2 + 1] = static_cast<uint8_t>(v[i] & 0xFF);
    }
    const bool ok = std::fwrite(be.data(), 1, be.size(), f) == be.size();
    return std::fclose(f) == 0 && ok;
}
bool writePnm8(const std::string &path, const uint8_t *v, int w, int h, int channels) {
    FILE *f = openFileUtf8(path, "wb");
    if (!f) return false;
    std::fprintf(f, "%s\n%d %d\n255\n", channels == 3 ? "P6" : "P5", w, h);
    const size_t n = static_cast<size_t>(w) * h * channels;
    const bool ok = std::fwrite(v, 1, n, f) == n;
    return std::fclose(f) == 0 && ok;
}
bool writeBin(const std::string &path, const std::vector<uint8_t> &d) {
    FILE *f = openFileUtf8(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    return std::fclose(f) == 0 && ok;
}
} // namespace

FrameSource::~FrameSource() { stopRecording(); }

void FrameSource::resetStats() {
    depthFrames_ = 0; videoFrames_ = 0; depthFps_ = 0; videoFps_ = 0;
    depthWin_ = videoWin_ = 0;
    lastDepthNs_ = 0; lastVideoNs_ = 0;
    validRatio_ = 0; minMm_ = 0; maxMm_ = 0; centerMm_ = 0; videoMean_ = 0;
    videoW_ = 0; videoH_ = 0; videoFmt_ = 0;  // o modo de vídeo pode ter mudado
    dead_ = false;
}

void FrameSource::onDepth(const uint16_t *raw, int w, int h, uint32_t ts) {
    const int64_t now = monotonicNs();
    store_.putDepth(raw, w, h, ts, now);
    if (recDepth_) {
        std::lock_guard<std::mutex> lk(recMu_);
        if (rec_) rec_->push(RecordType::Depth, raw, static_cast<uint32_t>(w * h * 2), w, h, ts, now);
    }

    // Estatística amostrada (1 a cada 16 pixels) em mm reais via calibração.
    uint64_t valid = 0, total = 0;
    uint16_t mn = 0xFFFF, mx = 0;
    for (int y = 0; y < h; y += 4)
        for (int x = 0; x < w; x += 4) {
            ++total;
            const uint16_t mm = calib_.mm(raw[y * w + x]);
            if (!mm) continue;
            ++valid;
            mn = std::min(mn, mm);
            mx = std::max(mx, mm);
        }
    uint64_t sum = 0; int n = 0;
    for (int y = h / 2 - 4; y <= h / 2 + 4; ++y)
        for (int x = w / 2 - 4; x <= w / 2 + 4; ++x) {
            const uint16_t mm = calib_.mm(raw[y * w + x]);
            if (mm) { sum += mm; ++n; }
        }
    validRatio_ = total ? static_cast<double>(valid) / total : 0.0;
    minMm_ = valid ? mn : 0;
    maxMm_ = valid ? mx : 0;
    centerMm_ = n ? static_cast<double>(sum) / n : 0.0;
    depthTs_ = ts;
    lastDepthNs_ = now;
    depthFrames_++;
    updateFps(now, depthWin_, depthWinCount_, depthFps_);
}

void FrameSource::onVideo(const uint8_t *data, size_t bytes, VideoFormat f, int w, int h, uint32_t ts) {
    const int64_t now = monotonicNs();
    store_.putVideo(data, bytes, f, w, h, ts, now);
    if (recVideo_) {
        std::lock_guard<std::mutex> lk(recMu_);
        if (rec_) rec_->push(f == VideoFormat::Rgb ? RecordType::Rgb : RecordType::Ir8, data,
                             static_cast<uint32_t>(bytes), w, h, ts, now);
    }
    uint64_t sum = 0, cnt = 0;
    for (size_t i = 0; i < bytes; i += 37) { sum += data[i]; ++cnt; }
    videoMean_ = cnt ? static_cast<double>(sum) / cnt : 0.0;
    videoW_ = w; videoH_ = h; videoFmt_ = static_cast<int>(f);
    videoTs_ = ts;
    lastVideoNs_ = now;
    videoFrames_++;
    updateFps(now, videoWin_, videoWinCount_, videoFps_);
}

void FrameSource::readStats(double *o) const {
    for (int i = 0; i < S_COUNT; ++i) o[i] = 0;
    o[S_DEPTH_FRAMES] = static_cast<double>(depthFrames_.load());
    o[S_VIDEO_FRAMES] = static_cast<double>(videoFrames_.load());
    o[S_DEPTH_FPS] = depthFps_.load();
    o[S_VIDEO_FPS] = videoFps_.load();
    o[S_DEPTH_VALID_RATIO] = validRatio_.load();
    o[S_DEPTH_MIN_MM] = minMm_.load();
    o[S_DEPTH_MAX_MM] = maxMm_.load();
    o[S_DEPTH_CENTER_MM] = centerMm_.load();
    o[S_VIDEO_MEAN] = videoMean_.load();
    const int64_t a = lastDepthNs_.load(), b = lastVideoNs_.load();
    o[S_SYNC_DELTA_MS] = (a && b) ? std::llabs(a - b) / 1e6 : -1.0;
    o[S_DEVICE_DEAD] = dead_.load() ? 1.0 : 0.0;
    o[S_DEPTH_TS] = depthTs_.load();
    o[S_VIDEO_TS] = videoTs_.load();
    o[S_VIDEO_W] = videoW_.load();
    o[S_VIDEO_H] = videoH_.load();
    o[S_VIDEO_FORMAT] = videoFmt_.load();
    std::lock_guard<std::mutex> lk(recMu_);
    if (rec_) {
        const auto s = rec_->stats();
        o[S_REC_ACTIVE] = s.active ? 1 : 0;
        o[S_REC_FRAMES] = static_cast<double>(s.framesWritten);
        o[S_REC_DROPPED] = static_cast<double>(s.framesDropped);
        o[S_REC_BYTES] = static_cast<double>(s.bytesWritten);
        o[S_REC_IO_ERROR] = s.ioError ? 1 : 0;
    }
}

uint64_t FrameSource::renderDepth(const DepthViewParams &p, uint8_t *rgba, int w, int h, int stride, uint64_t afterSeq) {
    if (!store_.copyDepth(rDepth_, afterSeq)) return 0;
    if (rDepth_.width != w || rDepth_.height != h) return 0;
    const size_t n = static_cast<size_t>(w) * h;
    rMm_.resize(n); rMmFilt_.resize(n);
    rawToMm(rDepth_.raw.data(), n, calib_, rMm_.data());
    filterDepthMm(rMm_.data(), w, h, p.filter, rMmFilt_.data());
    depthMmToRgba(rMmFilt_.data(), w, h, p, rgba, stride);
    return rDepth_.seq;
}

uint64_t FrameSource::renderVideo(const VideoViewParams &p, uint8_t *rgba, int w, int h, int stride, uint64_t afterSeq) {
    if (!store_.copyVideo(rVideo_, afterSeq)) return 0;
    if (rVideo_.width != w || rVideo_.height != h) return 0;
    if (rVideo_.format == VideoFormat::Rgb) rgbToRgba(rVideo_.data.data(), w, h, p, rgba, stride);
    else grayToRgba(rVideo_.data.data(), w, h, p, rgba, stride);
    return rVideo_.seq;
}

uint64_t FrameSource::renderAligned(const VideoViewParams &p, uint8_t *rgba, int w, int h, int stride, uint64_t afterSeq) {
    // Atualiza quando chega profundidade nova; usa o RGB mais recente.
    if (!calib_.valid || w != kDepthW || h != kDepthH) return 0;
    if (!store_.copyDepth(rDepth_, afterSeq)) return 0;
    if (!store_.copyVideo(rVideo_, 0) || rVideo_.format != VideoFormat::Rgb ||
        rVideo_.width != kDepthW || rVideo_.height != kDepthH) return 0;
    const size_t n = static_cast<size_t>(w) * h;
    rMm_.resize(n);
    rawToMm(rDepth_.raw.data(), n, calib_, rMm_.data());
    rAligned_.resize(n * 3);
    alignRgbToDepth(rMm_.data(), rVideo_.data.data(), calib_, rAligned_.data());
    rgbToRgba(rAligned_.data(), w, h, p, rgba, stride);
    return rDepth_.seq;
}

void FrameSource::probe(int x, int y, double out[6]) const {
    for (int i = 0; i < 6; ++i) out[i] = 0;
    DepthFrame d;
    if (!store_.copyDepth(d) || x < 0 || y < 0 || x >= d.width || y >= d.height) return;
    const uint16_t raw = d.raw[static_cast<size_t>(y) * d.width + x];
    const uint16_t mm = calib_.mm(raw);
    out[1] = raw;
    out[2] = mm;
    if (!mm) return;
    const Point3 p = calib_.toWorld(x, y, mm);
    out[0] = 1; out[3] = p.x; out[4] = p.y; out[5] = p.z;
}

std::string FrameSource::capture(const std::string &dir, DepthFilter filter, std::string &err) {
    DepthFrame d;
    VideoFrame v;
    const bool hasD = store_.copyDepth(d);
    const bool hasV = store_.copyVideo(v);
    if (!hasD && !hasV) { err = "Nenhum quadro recebido ainda"; return ""; }

    std::string files;
    auto add = [&](const std::string &name) { files += (files.empty() ? "\"" : ",\"") + name + "\""; };
    bool ok = true;
    if (hasD) {
        const size_t n = static_cast<size_t>(d.width) * d.height;
        ok &= writePgm16(dir + "/depth_raw.pgm", d.raw.data(), d.width, d.height, 2047); add("depth_raw.pgm");
        std::vector<uint16_t> mm(n), filt(n);
        rawToMm(d.raw.data(), n, calib_, mm.data());
        ok &= writePgm16(dir + "/depth_mm.pgm", mm.data(), d.width, d.height, kMaxMetricMm); add("depth_mm.pgm");
        if (filter != DepthFilter::None) {
            filterDepthMm(mm.data(), d.width, d.height, filter, filt.data());
            ok &= writePgm16(dir + "/depth_mm_filtrado.pgm", filt.data(), d.width, d.height, kMaxMetricMm);
            add("depth_mm_filtrado.pgm");
        }
        if (hasV && v.format == VideoFormat::Rgb && calib_.valid && v.width == kDepthW && v.height == kDepthH) {
            std::vector<uint8_t> al(n * 3);
            alignRgbToDepth(mm.data(), v.data.data(), calib_, al.data());
            ok &= writePnm8(dir + "/rgb_alinhado.ppm", al.data(), d.width, d.height, 3); add("rgb_alinhado.ppm");
        }
    }
    if (hasV) {
        if (v.format == VideoFormat::Rgb) { ok &= writePnm8(dir + "/rgb.ppm", v.data.data(), v.width, v.height, 3); add("rgb.ppm"); }
        else { ok &= writePnm8(dir + "/ir.pgm", v.data.data(), v.width, v.height, 1); add("ir.pgm"); }
    }
    if (calib_.valid) { ok &= writeBin(dir + "/calibration.bin", calib_.serialize()); add("calibration.bin"); }
    if (!ok) { err = "Falha ao gravar arquivos (armazenamento cheio?)"; return ""; }

    auto frameJson = [](int w, int h, const std::string &fmt, uint32_t ts, int64_t ns) {
        return "{\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) + ",\"format\":\"" + fmt +
               "\",\"sensor_timestamp\":" + std::to_string(ts) + ",\"host_ns\":" + std::to_string(ns) + "}";
    };
    char delta[32] = "null";
    if (hasD && hasV) std::snprintf(delta, sizeof(delta), "%.3f", std::llabs(d.hostNs - v.hostNs) / 1e6);
    std::string json = "{\"files\":[" + files + "]";
    json += ",\"depth\":" + (hasD ? frameJson(d.width, d.height, "raw11 (2047 = sem medida)", d.deviceTs, d.hostNs) : std::string("null"));
    json += ",\"video\":" + (hasV ? frameJson(v.width, v.height, v.format == VideoFormat::Rgb ? "rgb888" : "ir8", v.deviceTs, v.hostNs) : std::string("null"));
    json += std::string(",\"sync_delta_ms\":") + delta;
    json += ",\"intrinsics\":" + (calib_.valid ? calib_.intrinsicsJson() : std::string("null")) + "}";
    return json;
}

bool FrameSource::startRecording(const std::string &path, const std::string &json, bool depth, bool video,
                                 std::string &err) {
    std::lock_guard<std::mutex> lk(recMu_);
    if (rec_ && rec_->active()) { err = "Já existe uma gravação em andamento"; return false; }
    rec_ = std::make_unique<Recorder>();
    if (!rec_->start(path, json, calib_.valid ? calib_.serialize() : std::vector<uint8_t>{}, err)) {
        rec_.reset();
        return false;
    }
    recDepth_ = depth;
    recVideo_ = video;
    return true;
}

void FrameSource::stopRecording() {
    recDepth_ = false;
    recVideo_ = false;
    {
        std::lock_guard<std::mutex> lk(recMu_);
        if (rec_) rec_->stop();  // mantém o objeto para as estatísticas finais
    }
}

} // namespace k3d
