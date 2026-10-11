// Testes do núcleo de sensores no host: calibração, colorização, filtros,
// alinhamento, gravação/leitura .k3drec, reprodução e captura.
// Usa uma calibração sintética conhecida (não é dado de sensor).
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "calibration.h"
#include "colorize.h"
#include "frame_source.h"
#include "playback_source.h"
#include "recording.h"

using namespace k3d;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static Calibration syntheticCalibration() {
    Calibration c;
    c.referenceDistanceMm = 120.f;
    c.referencePixelSizeMm = 0.1042f;
    c.dcmosEmitterDistCm = 7.5f;
    c.dcmosRcmosDistCm = 2.3f;
    c.rawToMm.resize(kRawLutSize);
    for (int r = 0; r < kRawLutSize; ++r) c.rawToMm[r] = r < 2000 ? static_cast<uint16_t>(500 + r * 3) : 0;
    c.rawToMm[kRawNoValue] = 0;
    c.depthToRgbShift.assign(kMaxMetricMm, 0);
    c.regTable.resize(static_cast<size_t>(kDepthW) * kDepthH * 2);
    for (int y = 0; y < kDepthH; ++y)
        for (int x = 0; x < kDepthW; ++x) {
            c.regTable[(y * kDepthW + x) * 2] = x * kRegXScale;  // identidade
            c.regTable[(y * kDepthW + x) * 2 + 1] = y;
        }
    c.valid = true;
    return c;
}

// Fonte de teste: injeta quadros como se viessem do sensor.
class TestSource : public FrameSource {
public:
    explicit TestSource(const Calibration &c) { calib_ = c; }
    bool isPlayback() const override { return false; }
    void depth(const uint16_t *raw, uint32_t ts) { onDepth(raw, kDepthW, kDepthH, ts); }
    void rgb(const uint8_t *d, uint32_t ts) { onVideo(d, kDepthW * kDepthH * 3, VideoFormat::Rgb, kDepthW, kDepthH, ts); }
};

static bool exists(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0; }

static void testCalibration() {
    const Calibration c = syntheticCalibration();
    const auto blob = c.serialize();
    const Calibration d = Calibration::deserialize(blob.data(), blob.size());
    CHECK(d.valid);
    CHECK(d.rawToMm == c.rawToMm && d.regTable == c.regTable && d.depthToRgbShift == c.depthToRgbShift);
    CHECK(std::fabs(d.fx() - 120.0 / (2 * 0.1042)) < 1e-3);
    CHECK(d.mm(kRawNoValue) == 0 && d.mm(100) == 800);

    const Point3 center = c.toWorld(320, 240, 2000);
    CHECK(std::fabs(center.x) < 1e-9 && std::fabs(center.y) < 1e-9 && center.z == 2000);
    const Point3 p = c.toWorld(320 + c.fx(), 240, 1500);  // um "f" à direita: X = Z
    CHECK(std::fabs(p.x - 1500) < 1e-6);

    std::vector<uint8_t> broken(blob.begin(), blob.begin() + 100);
    CHECK(!Calibration::deserialize(broken.data(), broken.size()).valid);
    int rx, ry;
    CHECK(c.depthToRgbPixel(10, 20, 1000, rx, ry) && rx == 10 && ry == 20);
    CHECK(!c.depthToRgbPixel(10, 20, 0, rx, ry));
    std::printf("calibração OK (fx=%.2f)\n", c.fx());
}

static void testColorizeAndFilters() {
    const int w = 8, h = 8;
    std::vector<uint16_t> mm(w * h, 1000), out(w * h);
    mm[0] = 0;          // sem medida
    mm[1] = 9000;       // fora da faixa
    mm[2] = 500;        // limite próximo
    std::vector<uint8_t> rgba(w * h * 4);
    DepthViewParams p; p.minMm = 500; p.maxMm = 4500; p.colormap = Colormap::Gray;
    depthMmToRgba(mm.data(), w, h, p, rgba.data(), w * 4);
    CHECK(rgba[0] == 0 && rgba[3] == 255);
    CHECK(rgba[4] == 38);
    CHECK(rgba[8] == 255);  // perto = claro

    // Mediana não preenche buracos e remove ruído impulsivo
    std::fill(mm.begin(), mm.end(), 1000);
    mm[3 * w + 3] = 3000;  // pico isolado
    mm[5 * w + 5] = 0;     // buraco
    filterDepthMm(mm.data(), w, h, DepthFilter::Median3, out.data());
    CHECK(out[3 * w + 3] == 1000);
    CHECK(out[5 * w + 5] == 0);

    // Remoção de bordas elimina o pixel "voador"
    const size_t removed = filterDepthMm(mm.data(), w, h, DepthFilter::RemoveEdges, out.data());
    CHECK(out[3 * w + 3] == 0 && removed >= 1);
    CHECK(out[2 * w + 2] == 1000);

    // Ajuste de vídeo não altera a fonte
    std::vector<uint8_t> rgb = {10, 128, 250};
    std::vector<uint8_t> o(4);
    rgbToRgba(rgb.data(), 1, 1, VideoViewParams{0.f, 2.f}, o.data(), 4);
    CHECK(rgb[0] == 10 && o[0] == 0 && o[1] == 128 && o[2] == 255);
    std::printf("colorização e filtros OK\n");
}

static void testAlignment() {
    const Calibration c = syntheticCalibration();
    std::vector<uint16_t> mm(kDepthW * kDepthH, 1200);
    mm[0] = 0;
    std::vector<uint8_t> rgb(kDepthW * kDepthH * 3), out(kDepthW * kDepthH * 3);
    for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<uint8_t>(i * 7);
    const size_t colored = alignRgbToDepth(mm.data(), rgb.data(), c, out.data());
    CHECK(colored == static_cast<size_t>(kDepthW * kDepthH - 1));
    CHECK(out[0] == 0 && out[3] == rgb[3]);
    std::printf("alinhamento OK\n");
}

static std::string tmpDir(const char *name) {
    std::string d = std::string("/tmp/k3d_test_") + name + "_" + std::to_string(getpid());
    mkdir(d.c_str(), 0755);
    return d;
}

static void testRecordPlaybackCapture() {
    const Calibration c = syntheticCalibration();
    const std::string dir = tmpDir("rec");
    const std::string path = dir + "/t.k3drec";

    std::vector<uint16_t> raw(kDepthW * kDepthH);
    std::vector<uint8_t> rgb(kDepthW * kDepthH * 3, 77);
    {
        TestSource src(c);
        std::string err;
        CHECK(src.startRecording(path, "{\"teste\":true}", true, true, err));
        for (int f = 0; f < 20; ++f) {
            for (int i = 0; i < kDepthW * kDepthH; ++i) raw[i] = static_cast<uint16_t>(100 + f);  // mm = 800 + 3f
            raw[0] = kRawNoValue;
            src.depth(raw.data(), 1000u + f);
            src.rgb(rgb.data(), 1000u + f);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        src.stopRecording();
        double st[S_COUNT];
        src.readStats(st);
        CHECK(st[S_REC_FRAMES] == 40 && st[S_REC_DROPPED] == 0);

        // Captura de quadro: arquivos e JSON
        const std::string cap = tmpDir("cap");
        const std::string json = src.capture(cap, DepthFilter::Median3, err);
        CHECK(!json.empty());
        CHECK(json.find("\"intrinsics\"") != std::string::npos && json.find("sync_delta_ms") != std::string::npos);
        for (const char *f : {"depth_raw.pgm", "depth_mm.pgm", "depth_mm_filtrado.pgm", "rgb.ppm", "rgb_alinhado.ppm", "calibration.bin"})
            CHECK(exists(cap + "/" + f));
        FILE *pf = std::fopen((cap + "/depth_mm.pgm").c_str(), "rb");
        char hdr[32] = {0};
        CHECK(std::fread(hdr, 1, 18, pf) == 18 && std::strncmp(hdr, "P5\n640 480\n10000\n", 17) == 0);
        std::fclose(pf);

        double pr[6];
        src.probe(320, 240, pr);
        CHECK(pr[0] == 1 && pr[1] == 119 && pr[2] == 857 && std::fabs(pr[3]) < 1e-9);
        src.probe(0, 0, pr);
        CHECK(pr[0] == 0 && pr[1] == kRawNoValue);
    }

    RecordingReader rd;
    std::string err;
    CHECK(rd.open(path, err));
    CHECK(rd.index().size() == 40 && !rd.truncated() && rd.json() == "{\"teste\":true}");

    // Arquivo cortado no meio do último quadro: lido até o último completo
    {
        FILE *f = std::fopen(path.c_str(), "rb");
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fclose(f);
        CHECK(truncate(path.c_str(), size - 1000) == 0);
        RecordingReader r2;
        CHECK(r2.open(path, err));
        CHECK(r2.index().size() == 39 && r2.truncated());
    }

    // Reprodução entrega quadros com a calibração da gravação
    PlaybackSource pb;
    CHECK(pb.open(path, err));
    pb.setSpeed(4.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    double st[S_COUNT];
    pb.readStats(st);
    CHECK(st[S_DEPTH_FRAMES] > 0 && st[S_VIDEO_FRAMES] > 0);
    CHECK(st[S_PLAY_TRUNCATED] == 1);
    CHECK(pb.calibration().valid && std::fabs(pb.calibration().fx() - c.fx()) < 1e-6);
    std::vector<uint8_t> rgba(kDepthW * kDepthH * 4);
    DepthViewParams dp;
    CHECK(pb.renderDepth(dp, rgba.data(), kDepthW, kDepthH, kDepthW * 4, 0) > 0);
    CHECK(pb.renderAligned(VideoViewParams{}, rgba.data(), kDepthW, kDepthH, kDepthW * 4, 0) > 0);
    CHECK(rgba[(240 * kDepthW + 320) * 4] == 77);
    pb.close();
    std::printf("gravação, reprodução e captura OK\n");

    PlaybackSource bad;
    CHECK(!bad.open(dir + "/nao_existe.k3drec", err));
}

int main() {
    testCalibration();
    testColorizeAndFilters();
    testAlignment();
    testRecordPlaybackCapture();
    std::printf("OK\n");
    return 0;
}
