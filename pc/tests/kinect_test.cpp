// Detecção do Kinect por VID/PID, abertura sem dispositivo e fonte de profundidade.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "kinect/body_source.h"
#include "kinect/device_watch.h"
#include "kinect/pc_session.h"

using namespace k3d;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

class InjectSource : public FrameSource {
public:
    explicit InjectSource(const Calibration &c) { calib_ = c; }
    bool isPlayback() const override { return false; }
    void depth(const uint16_t *raw) { onDepth(raw, 640, 480, 1); }
};

int main() {
    using S = KinectUsbStatus::State;
    CHECK(classifyKinect({}).state == S::None);
    CHECK(classifyKinect({{0x1234, 0x02AE, 0x010B, 1, 2}}).state == S::None);
    auto a = classifyKinect({{0x045E, 0x02B0, 0, 1, 2}, {0x045E, 0x02AE, 0x010B, 1, 3}});
    CHECK(a.state == S::V1Ready && a.model1414 && a.motorPresent);
    auto b = classifyKinect({{0x045E, 0x02AE, 0x010F, 1, 3}});
    CHECK(b.state == S::V1Ready && !b.model1414 && b.modelName().find("1473") != std::string::npos);
    CHECK(classifyKinect({{0x045E, 0x02B0, 0, 1, 2}}).state == S::V1NoPower);
    CHECK(classifyKinect({{0x045E, 0x02D8, 0, 1, 2}}).state == S::V2);
    CHECK(classifyKinect({{0x045E, 0x097C, 0, 1, 2}}).state == S::Azure);
    CHECK(classifyKinect({{0x045E, 0x02BF, 0, 1, 2}}).modelName() == "Kinect for Windows v1");

    // vigia sem Kinect: não trava e informa "nenhum"
    DeviceWatcher w;
    w.start(100);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto st = w.status();
    CHECK(st.state == S::None || st.state == S::UsbError);
    w.stop();

    // abrir sem Kinect falha com mensagem, sem travar
    PcKinectSession s;
    std::string err;
    CHECK(!s.open(true, err));
    CHECK(!err.empty() && !s.isOpen());
    CHECK(!kinectOpenHint("LIBUSB_ERROR_ACCESS").empty());

    // FrameSourceFeed converte bruto -> mm pela calibração
    Calibration c;
    c.referenceDistanceMm = 120.f;
    c.referencePixelSizeMm = 0.1042f;
    c.rawToMm.assign(2048, 0);
    for (int i = 0; i < 2047; ++i) c.rawToMm[i] = static_cast<uint16_t>(400 + i * 2);
    c.depthToRgbShift.assign(10000, 0);
    c.regTable.assign(640 * 480 * 2, 0);
    c.valid = true;
    InjectSource src(c);
    FrameSourceFeed feed(&src, false);
    std::vector<uint16_t> mm;
    Intrinsics in;
    CHECK(!feed.fetch(mm, in));
    std::vector<uint16_t> raw(640 * 480, 100);
    raw[5] = 2047;
    src.depth(raw.data());
    CHECK(feed.fetch(mm, in));
    CHECK(mm[0] == 600 && mm[5] == 0);
    CHECK(in.fx > 570 && in.fx < 580 && in.width == 640);
    CHECK(!feed.fetch(mm, in));  // sem quadro novo
    std::printf("OK\n");
    return 0;
}
