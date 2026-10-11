#pragma once
// Fontes de profundidade para o rastreador corporal:
//   - Kinect ao vivo ou gravação .k3drec (FrameSource)
//   - Simulação (boneco sintético controlado por teclado/mouse ou pelo robô)
#include <cstdint>
#include <vector>

#include "body/body_tracker.h"
#include "body/sim_depth.h"
#include "frame_source.h"

namespace k3d {

class DepthFeed {
public:
    virtual ~DepthFeed() = default;
    // Copia a profundidade mais nova (mm). false se não há quadro novo.
    virtual bool fetch(std::vector<uint16_t> &mm, Intrinsics &intr) = 0;
    virtual const ColorLookup *color() { return nullptr; }
    // Segmentação/esqueletos externos (NiTE2) do mesmo quadro; nulo nas outras fontes.
    virtual const ExternalBodies *external() { return nullptr; }
    virtual bool simulated() const = 0;
};

// Kinect real ou gravação: usa a calibração de fábrica para mm e cor.
class FrameSourceFeed : public DepthFeed, public ColorLookup {
public:
    explicit FrameSourceFeed(FrameSource *src, bool isRecording) : src_(src), recording_(isRecording) {}
    bool fetch(std::vector<uint16_t> &mm, Intrinsics &intr) override;
    const ColorLookup *color() override;
    bool simulated() const override { return recording_; }
    bool color(int u, int v, uint16_t mm, uint8_t out[3]) const override;
    void setColorEnabled(bool on) { colorOn_ = on; }

private:
    FrameSource *src_;
    bool recording_;
    bool colorOn_ = true;
    DepthFrame df_;
    VideoFrame vf_;
    uint64_t lastSeq_ = 0;
    bool haveRgb_ = false;
};

// Simulação: renderiza bonecos a 30 Hz.
class SimFeed : public DepthFeed {
public:
    SimFeed() { scene.fast = true; }
    bool fetch(std::vector<uint16_t> &mm, Intrinsics &intr) override;
    bool simulated() const override { return true; }
    void advance(float dt) { acc_ += dt; }
    SimScene scene;
    SimPose poses[2];
    int count = 1;

private:
    SimDepth sim_;
    float acc_ = 1.f;
};

} // namespace k3d
