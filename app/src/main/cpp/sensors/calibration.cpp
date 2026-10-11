#include "calibration.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include "libfreenect.h"
#include "libfreenect_registration.h"
}

namespace k3d {

namespace {
constexpr uint32_t kMagic = 0x4B334341;  // "K3CA"
constexpr uint32_t kVersion = 1;

template <typename T> void put(std::vector<uint8_t> &o, const T &v) {
    const auto *p = reinterpret_cast<const uint8_t *>(&v);
    o.insert(o.end(), p, p + sizeof(T));
}
template <typename T> void putArr(std::vector<uint8_t> &o, const std::vector<T> &v) {
    put<uint32_t>(o, static_cast<uint32_t>(v.size()));
    const auto *p = reinterpret_cast<const uint8_t *>(v.data());
    o.insert(o.end(), p, p + v.size() * sizeof(T));
}
struct Reader {
    const uint8_t *p; size_t n; size_t i = 0; bool ok = true;
    template <typename T> T get() {
        T v{};
        if (i + sizeof(T) > n) { ok = false; return v; }
        std::memcpy(&v, p + i, sizeof(T)); i += sizeof(T);
        return v;
    }
    template <typename T> std::vector<T> getArr(size_t expected) {
        const uint32_t c = get<uint32_t>();
        if (!ok || c != expected || i + c * sizeof(T) > n) { ok = false; return {}; }
        std::vector<T> v(c);
        std::memcpy(v.data(), p + i, c * sizeof(T)); i += c * sizeof(T);
        return v;
    }
};
} // namespace

bool Calibration::depthToRgbPixel(int x, int y, uint16_t mmv, int &rx, int &ry) const {
    if (!valid || mmv == 0 || mmv >= kMaxMetricMm) return false;
    const size_t idx = static_cast<size_t>(y) * kDepthW + x;
    const int32_t nx = (regTable[idx * 2] + depthToRgbShift[mmv]) / kRegXScale;
    const int64_t target = static_cast<int64_t>(regTable[idx * 2 + 1]) * kDepthW + nx - targetOffset;
    if (nx < 0 || nx >= kDepthW || target < 0 || target >= kDepthW * kDepthH) return false;
    rx = static_cast<int>(target % kDepthW);
    ry = static_cast<int>(target / kDepthW);
    return true;
}

Calibration Calibration::fromDevice(_freenect_device *dev) {
    Calibration c;
    if (!dev) return c;
    freenect_registration reg = freenect_copy_registration(dev);
    if (!reg.raw_to_mm_shift || !reg.depth_to_rgb_shift || !reg.registration_table) {
        freenect_destroy_registration(&reg);
        return c;
    }
    c.dcmosEmitterDistCm = reg.zero_plane_info.dcmos_emitter_dist;
    c.dcmosRcmosDistCm = reg.zero_plane_info.dcmos_rcmos_dist;
    c.referenceDistanceMm = reg.zero_plane_info.reference_distance;
    c.referencePixelSizeMm = reg.zero_plane_info.reference_pixel_size;
    c.targetOffset = static_cast<uint32_t>(reg.reg_pad_info.start_lines) * kDepthH;
    c.rawToMm.assign(reg.raw_to_mm_shift, reg.raw_to_mm_shift + kRawLutSize);
    c.depthToRgbShift.assign(reg.depth_to_rgb_shift, reg.depth_to_rgb_shift + kMaxMetricMm);
    const int32_t *t = &reg.registration_table[0][0];
    c.regTable.assign(t, t + kDepthW * kDepthH * 2);
    freenect_destroy_registration(&reg);
    c.valid = c.referencePixelSizeMm > 0 && c.referenceDistanceMm > 0;
    return c;
}

std::vector<uint8_t> Calibration::serialize() const {
    std::vector<uint8_t> o;
    o.reserve(16 + rawToMm.size() * 2 + (depthToRgbShift.size() + regTable.size()) * 4 + 64);
    put(o, kMagic);
    put(o, kVersion);
    put(o, dcmosEmitterDistCm);
    put(o, dcmosRcmosDistCm);
    put(o, referenceDistanceMm);
    put(o, referencePixelSizeMm);
    put(o, targetOffset);
    putArr(o, rawToMm);
    putArr(o, depthToRgbShift);
    putArr(o, regTable);
    return o;
}

Calibration Calibration::deserialize(const uint8_t *data, size_t len) {
    Calibration c;
    Reader r{data, len};
    if (r.get<uint32_t>() != kMagic || r.get<uint32_t>() != kVersion) return c;
    c.dcmosEmitterDistCm = r.get<float>();
    c.dcmosRcmosDistCm = r.get<float>();
    c.referenceDistanceMm = r.get<float>();
    c.referencePixelSizeMm = r.get<float>();
    c.targetOffset = r.get<uint32_t>();
    c.rawToMm = r.getArr<uint16_t>(kRawLutSize);
    c.depthToRgbShift = r.getArr<int32_t>(kMaxMetricMm);
    c.regTable = r.getArr<int32_t>(static_cast<size_t>(kDepthW) * kDepthH * 2);
    c.valid = r.ok && c.referencePixelSizeMm > 0;
    if (!c.valid) return Calibration{};
    return c;
}

std::string Calibration::intrinsicsJson() const {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "{\"width\":%d,\"height\":%d,\"fx\":%.6f,\"fy\":%.6f,\"cx\":%.1f,\"cy\":%.1f,"
                  "\"units\":\"mm\",\"axes\":\"x direita, y para baixo, z para frente (camera IR)\","
                  "\"reference_distance_mm\":%.6f,\"reference_pixel_size_mm\":%.8f,"
                  "\"dcmos_emitter_dist_cm\":%.4f,\"dcmos_rcmos_dist_cm\":%.4f}",
                  kDepthW, kDepthH, fx(), fy(), cx(), cy(), referenceDistanceMm, referencePixelSizeMm,
                  dcmosEmitterDistCm, dcmosRcmosDistCm);
    return buf;
}

} // namespace k3d
