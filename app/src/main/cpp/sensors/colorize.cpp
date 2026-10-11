#include "colorize.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace k3d {

namespace {
inline uint8_t clamp8(float v) { return static_cast<uint8_t>(std::min(255.f, std::max(0.f, v))); }

struct TurboLut {
    std::array<uint8_t, 256 * 3> c{};
    TurboLut() {
        for (int i = 0; i < 256; ++i) {
            const double x = i / 255.0;
            const double r = 0.13572138 + x * (4.61539260 + x * (-42.66032258 + x * (132.13108234 + x * (-152.94239396 + x * 59.28637943))));
            const double g = 0.09140261 + x * (2.19418839 + x * (4.84296658 + x * (-14.18503333 + x * (4.27729857 + x * 2.82956604))));
            const double b = 0.10667330 + x * (12.64194608 + x * (-60.58204836 + x * (110.36276771 + x * (-89.90310912 + x * 27.34824973))));
            c[i * 3 + 0] = clamp8(static_cast<float>(r * 255.0));
            c[i * 3 + 1] = clamp8(static_cast<float>(g * 255.0));
            c[i * 3 + 2] = clamp8(static_cast<float>(b * 255.0));
        }
    }
};
const TurboLut &turboLut() { static TurboLut l; return l; }

inline void adjustLut(const VideoViewParams &p, std::array<uint8_t, 256> &lut) {
    for (int v = 0; v < 256; ++v)
        lut[v] = clamp8((v - 128.f) * p.contrast + 128.f + p.brightness * 255.f);
}
} // namespace

void turbo(float t, uint8_t &r, uint8_t &g, uint8_t &b) {
    const int i = static_cast<int>(std::min(1.f, std::max(0.f, t)) * 255.f + 0.5f);
    const auto &c = turboLut().c;
    r = c[i * 3]; g = c[i * 3 + 1]; b = c[i * 3 + 2];
}

void rawToMm(const uint16_t *raw, size_t n, const Calibration &cal, uint16_t *outMm) {
    for (size_t i = 0; i < n; ++i) outMm[i] = cal.mm(raw[i]);
}

size_t filterDepthMm(const uint16_t *in, int w, int h, DepthFilter f, uint16_t *out) {
    std::memcpy(out, in, sizeof(uint16_t) * w * h);
    if (f == DepthFilter::None) return 0;
    size_t removed = 0;
    std::vector<uint16_t> tmp;
    const uint16_t *src = in;

    if (f == DepthFilter::Median3 || f == DepthFilter::MedianAndEdges) {
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                const uint16_t c = src[y * w + x];
                if (!c) continue;  // não preenche buracos
                uint16_t v[9]; int n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const uint16_t d = src[(y + dy) * w + x + dx];
                        if (d) v[n++] = d;
                    }
                std::nth_element(v, v + n / 2, v + n);
                out[y * w + x] = v[n / 2];
            }
        }
        if (f == DepthFilter::MedianAndEdges) {
            tmp.assign(out, out + static_cast<size_t>(w) * h);
            src = tmp.data();
        }
    }
    if (f == DepthFilter::RemoveEdges || f == DepthFilter::MedianAndEdges) {
        // Remove pixels "voadores" nas bordas de objetos: salto grande para um vizinho.
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                const int c = src[y * w + x];
                if (!c) continue;
                const int thr = std::max(40, c * 4 / 100);
                const int nb[4] = {src[y * w + x - 1], src[y * w + x + 1], src[(y - 1) * w + x], src[(y + 1) * w + x]};
                int jumps = 0, invalid = 0;
                for (int d : nb) {
                    if (!d) ++invalid;
                    else if (std::abs(d - c) > thr) ++jumps;
                }
                if (jumps >= 2 || (jumps >= 1 && invalid >= 2)) {
                    out[y * w + x] = 0;
                    ++removed;
                }
            }
        }
    }
    return removed;
}

void depthMmToRgba(const uint16_t *mm, int w, int h, const DepthViewParams &p, uint8_t *rgba, int stride) {
    const int lo = std::max(1, p.minMm);
    const int hi = std::max(lo + 1, p.maxMm);
    const float inv = 1.f / static_cast<float>(hi - lo);
    const auto &tl = turboLut().c;
    for (int y = 0; y < h; ++y) {
        uint8_t *row = rgba + static_cast<size_t>(y) * stride;
        for (int x = 0; x < w; ++x) {
            const int d = mm[y * w + x];
            uint8_t *o = row + x * 4;
            o[3] = 255;
            if (d == 0) { o[0] = o[1] = o[2] = 0; continue; }              // sem medida
            if (d < lo || d > hi) { o[0] = o[1] = o[2] = 38; continue; }    // fora da faixa
            const float t = (d - lo) * inv;                                 // 0 = perto
            const int i = static_cast<int>((1.f - t) * 255.f + 0.5f);
            if (p.colormap == Colormap::Turbo) {
                o[0] = tl[i * 3]; o[1] = tl[i * 3 + 1]; o[2] = tl[i * 3 + 2];
            } else {
                o[0] = o[1] = o[2] = static_cast<uint8_t>(i);
            }
        }
    }
}

void rgbToRgba(const uint8_t *rgb, int w, int h, const VideoViewParams &p, uint8_t *rgba, int stride) {
    std::array<uint8_t, 256> lut{};
    adjustLut(p, lut);
    for (int y = 0; y < h; ++y) {
        uint8_t *row = rgba + static_cast<size_t>(y) * stride;
        const uint8_t *s = rgb + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; ++x) {
            row[x * 4 + 0] = lut[s[x * 3 + 0]];
            row[x * 4 + 1] = lut[s[x * 3 + 1]];
            row[x * 4 + 2] = lut[s[x * 3 + 2]];
            row[x * 4 + 3] = 255;
        }
    }
}

void grayToRgba(const uint8_t *g, int w, int h, const VideoViewParams &p, uint8_t *rgba, int stride) {
    std::array<uint8_t, 256> lut{};
    adjustLut(p, lut);
    for (int y = 0; y < h; ++y) {
        uint8_t *row = rgba + static_cast<size_t>(y) * stride;
        const uint8_t *s = g + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            const uint8_t v = lut[s[x]];
            row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = v;
            row[x * 4 + 3] = 255;
        }
    }
}

size_t alignRgbToDepth(const uint16_t *mm, const uint8_t *rgb, const Calibration &cal, uint8_t *out) {
    std::memset(out, 0, static_cast<size_t>(kDepthW) * kDepthH * 3);
    if (!cal.valid) return 0;
    size_t colored = 0;
    for (int y = 0; y < kDepthH; ++y) {
        for (int x = 0; x < kDepthW; ++x) {
            const uint16_t d = mm[y * kDepthW + x];
            int rx, ry;
            if (!cal.depthToRgbPixel(x, y, d, rx, ry)) continue;
            const uint8_t *s = rgb + (static_cast<size_t>(ry) * kDepthW + rx) * 3;
            uint8_t *o = out + (static_cast<size_t>(y) * kDepthW + x) * 3;
            o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
            ++colored;
        }
    }
    return colored;
}

} // namespace k3d
