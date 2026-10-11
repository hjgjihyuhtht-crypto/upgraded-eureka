#include "kinect/body_source.h"

namespace k3d {

bool FrameSourceFeed::fetch(std::vector<uint16_t> &mm, Intrinsics &intr) {
    if (!src_ || !src_->store().copyDepth(df_, lastSeq_)) return false;
    lastSeq_ = df_.seq;
    const Calibration &cal = src_->calibration();
    const size_t n = static_cast<size_t>(df_.width) * df_.height;
    mm.resize(n);
    if (cal.valid) {
        for (size_t i = 0; i < n; ++i) mm[i] = cal.mm(df_.raw[i]);
        intr.fx = static_cast<float>(cal.fx());
        intr.fy = static_cast<float>(cal.fy());
        intr.cx = static_cast<float>(cal.cx());
        intr.cy = static_cast<float>(cal.cy());
    } else {
        // Sem calibração: aproximação publicada para o Kinect v1 (1/(a·raw+b), em metros)
        for (size_t i = 0; i < n; ++i) {
            const uint16_t r = df_.raw[i];
            const double z = r < 2047 ? 1.0 / (r * -0.0030711016 + 3.3309495161) : 0;
            mm[i] = z > 0 && z < 10 ? static_cast<uint16_t>(z * 1000) : 0;
        }
        intr = Intrinsics{};
    }
    intr.width = df_.width;
    intr.height = df_.height;
    // cor mais recente para colorir a silhueta
    haveRgb_ = colorOn_ && cal.valid && src_->store().copyVideo(vf_) && vf_.format == VideoFormat::Rgb &&
               vf_.width == kDepthW && vf_.height == kDepthH;
    return true;
}

const ColorLookup *FrameSourceFeed::color() { return haveRgb_ ? this : nullptr; }

bool FrameSourceFeed::color(int u, int v, uint16_t mm, uint8_t out[3]) const {
    int rx, ry;
    if (!haveRgb_ || !src_->calibration().depthToRgbPixel(u, v, mm, rx, ry)) return false;
    const uint8_t *p = &vf_.data[(static_cast<size_t>(ry) * vf_.width + rx) * 3];
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    return true;
}

bool SimFeed::fetch(std::vector<uint16_t> &mm, Intrinsics &intr) {
    if (acc_ < 1.f / 30.f) return false;
    acc_ = 0;
    sim_.render(scene, poses, count, mm);
    intr = scene.intr;
    return true;
}

} // namespace k3d
