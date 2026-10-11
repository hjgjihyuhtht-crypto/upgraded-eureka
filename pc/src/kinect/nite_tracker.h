#pragma once
// Rastreamento de esqueleto com OpenNI2 + NiTE2 (só Linux).
//
//   libOpenNI2.so  compilado por este projeto (Apache-2.0), fica ao lado do executável;
//   OpenNI2/Drivers/libFreenectDriver.so  driver do libfreenect que faz o OpenNI2 ver o Kinect v1;
//   libNiTE2.so    PROPRIETÁRIO (PrimeSense, descontinuado). Não é distribuído: o usuário copia a pasta
//                  Redist do NiTE-Linux-x64-2.2 para "nite/" ao lado do programa (ou define NITE2_PATH).
//
// O NiTE abre o Kinect pelo OpenNI2. Por isso este modo substitui a sessão do libfreenect enquanto
// está ativo (os dois não podem usar a câmera ao mesmo tempo).
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "body/body_tracker.h"
#include "kinect/body_source.h"

namespace k3d {

struct NiteLocation {
    bool found = false;
    std::string dir;       // pasta com libNiTE2.so e a subpasta NiTE2/ (dados)
    std::string problem;   // por que não foi encontrado / está incompleto
    std::vector<std::string> searched;
};

// Procura o NiTE2 em: $NITE2_PATH, $NITE2_REDIST64, $NITE2_REDIST, <programa>/nite,
// <dados>/nite (~/.local/share/kinect3dstudio/nite), ~/NiTE-Linux-*/Redist, /opt/NiTE-Linux-*/Redist.
NiteLocation locateNite(const std::string &dataDir);
std::string executableDir();

// OpenNI2: inicializa (uma vez) e lista as câmeras que os drivers enxergam.
bool openni2Init(std::string &err);
int openni2DeviceCount();
std::string openni2Version();

class NiteSession {
public:
    NiteSession() = default;
    ~NiteSession();
    NiteSession(const NiteSession &) = delete;
    NiteSession &operator=(const NiteSession &) = delete;

    // Carrega o NiTE, cria o rastreador de usuários (que abre o Kinect pelo OpenNI2) e começa a ler.
    bool open(const NiteLocation &loc, std::string &err, bool wantColor = true);
    void close();
    bool isOpen() const { return tracker_ != nullptr; }

    // Kinect parou de mandar quadros, ou os quadros do NiTE não fazem sentido (versão incompatível).
    bool deviceLost() const;
    bool incompatible() const { return incompatible_; }
    std::string version() const { return version_; }
    bool mirrored() const { return mirrored_; }
    uint64_t frames() const { return frames_; }

    // Cópia do quadro mais novo. false se não há quadro novo desde 'afterSeq'.
    struct Snapshot {
        uint64_t seq = 0;
        std::vector<uint16_t> depthMm;
        std::vector<uint16_t> userMap;
        Intrinsics intr;
        ExternalBodies bodies;   // userMap aponta para o vetor acima
        double recvTime = 0;
    };
    bool fetch(Snapshot &out, uint64_t afterSeq) const;
    bool copyRgb(std::vector<uint8_t> &rgb, int &w, int &h) const;

private:
    void readLoop();
    void colorLoop();

    void *lib_ = nullptr;
    void *tracker_ = nullptr;   // NiteUserTrackerHandle
    void *colorDev_ = nullptr;  // OniDeviceHandle
    void *colorStream_ = nullptr;
    std::thread th_, colorTh_;
    std::atomic<bool> running_{false};
    std::atomic<bool> readDone_{true}, colorDone_{true};
    std::atomic<bool> incompatible_{false};
    std::atomic<bool> mirrored_{false};
    std::atomic<uint64_t> frames_{0};
    std::atomic<int64_t> lastFrameNs_{0};
    int64_t openedNs_ = 0;
    std::string version_;

    mutable std::mutex mu_;
    Snapshot latest_;
    mutable std::mutex rgbMu_;
    std::vector<uint8_t> rgb_;
    int rgbW_ = 0, rgbH_ = 0;
    int64_t rgbNs_ = 0;

    struct Api;
    Api *api_ = nullptr;
};

// Fonte de profundidade para o App: profundidade + rótulos + esqueletos do NiTE2.
class NiteFeed : public DepthFeed, public ColorLookup {
public:
    explicit NiteFeed(NiteSession *s) : s_(s) {}
    bool fetch(std::vector<uint16_t> &mm, Intrinsics &intr) override;
    const ColorLookup *color() override { return haveRgb_ ? this : nullptr; }
    const ExternalBodies *external() override { return snap_.seq ? &snap_.bodies : nullptr; }
    bool simulated() const override { return false; }
    bool color(int u, int v, uint16_t mm, uint8_t out[3]) const override;

private:
    NiteSession *s_;
    NiteSession::Snapshot snap_;
    std::vector<uint8_t> rgb_;
    int rgbW_ = 0, rgbH_ = 0;
    bool haveRgb_ = false;
};

} // namespace k3d
