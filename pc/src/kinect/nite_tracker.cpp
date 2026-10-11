#include "kinect/nite_tracker.h"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "OniCAPI.h"
#include "kinect/nite_capi.h"
#include "kinect/pc_session.h"

namespace fs = std::filesystem;

namespace k3d {

namespace {

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void logLine(const std::string &s) { PcKinectSession::log("[NiTE2] " + s); }

// Campo de visão que o driver Freenect informa ao OpenNI2 (DepthStream.hpp). O NiTE converte
// profundidade <-> mundo com esses valores, então usamos os mesmos para ficar coerente.
constexpr float kHFov = 58.5f * 3.14159265f / 180.f;
constexpr float kVFov = 45.6f * 3.14159265f / 180.f;

Intrinsics intrinsicsFor(int w, int h) {
    Intrinsics in;
    in.width = w;
    in.height = h;
    in.fx = (w * 0.5f) / std::tan(kHFov * 0.5f);
    in.fy = (h * 0.5f) / std::tan(kVFov * 0.5f);
    in.cx = w * 0.5f;
    in.cy = h * 0.5f;
    return in;
}

bool fileExists(const fs::path &p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}
bool dirExists(const fs::path &p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

int gOniInit = 0;
std::mutex gOniMu;
} // namespace

// ------------------------------------------------------------------ localizar

std::string executableDir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    return fs::path(buf).parent_path().string();
}

NiteLocation locateNite(const std::string &dataDir) {
    NiteLocation loc;
    std::vector<std::string> cands;
    for (const char *env : {"NITE2_PATH", "NITE2_REDIST64", "NITE2_REDIST"})
        if (const char *v = std::getenv(env); v && *v) cands.push_back(v);
    cands.push_back(executableDir() + "/nite");
    if (!dataDir.empty()) cands.push_back(dataDir + "/nite");
    std::vector<std::string> roots;
    if (const char *home = std::getenv("HOME")) roots.push_back(home);
    roots.push_back("/opt");
    roots.push_back("/usr/local");
    for (const auto &r : roots) {
        std::error_code ec;
        for (fs::directory_iterator it(r, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.rfind("NiTE", 0) == 0 && it->is_directory(ec)) {
                cands.push_back((it->path() / "Redist").string());
                cands.push_back(it->path().string());
            }
        }
    }
    for (const auto &c : cands) {
        loc.searched.push_back(c);
        const fs::path d(c);
        if (!fileExists(d / "libNiTE2.so")) continue;
        if (!dirExists(d / "NiTE2")) {
            if (loc.problem.empty())
                loc.problem = "Achei libNiTE2.so em " + c + ", mas falta a pasta de dados \"NiTE2\" ao lado dela. "
                              "Copie a pasta Redist inteira do NiTE.";
            continue;
        }
        loc.found = true;
        loc.dir = fs::absolute(d).lexically_normal().string();
        if (!loc.dir.empty() && loc.dir.back() == '/') loc.dir.pop_back();
        loc.problem.clear();
        return loc;
    }
    if (loc.problem.empty())
        loc.problem = "NiTE2 não encontrado. Copie o conteúdo da pasta Redist do NiTE-Linux-x64-2.2 para a pasta "
                      "\"nite\" ao lado do programa (ou defina NITE2_PATH).";
    return loc;
}

// -------------------------------------------------------------------- OpenNI2

bool openni2Init(std::string &err) {
    std::lock_guard<std::mutex> lk(gOniMu);
    const OniStatus rc = oniInitialize(ONI_API_VERSION);
    if (rc != ONI_STATUS_OK) {
        err = std::string("OpenNI2 não iniciou: ") + oniGetExtendedError();
        oniShutdown();
        return false;
    }
    ++gOniInit;
    return true;
}

static void openni2Shutdown() {
    std::lock_guard<std::mutex> lk(gOniMu);
    if (gOniInit > 0) { --gOniInit; oniShutdown(); }
}

int openni2DeviceCount() {
    OniDeviceInfo *list = nullptr;
    int n = 0;
    if (oniGetDeviceList(&list, &n) != ONI_STATUS_OK) return 0;
    oniReleaseDeviceList(list);
    return n;
}

std::string openni2Version() {
    const OniVersion v = oniGetVersion();
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.maintenance) + "." +
           std::to_string(v.build);
}

// --------------------------------------------------------------------- sessão

struct NiteSession::Api {
    PFN_niteInitialize initialize = nullptr;
    PFN_niteShutdown shutdown = nullptr;
    PFN_niteGetVersion getVersion = nullptr;
    PFN_niteInitializeUserTracker initTracker = nullptr;
    PFN_niteShutdownUserTracker shutdownTracker = nullptr;
    PFN_niteStartSkeletonTracking startSkeleton = nullptr;
    PFN_niteSetSkeletonSmoothing smoothing = nullptr;
    PFN_niteReadUserTrackerFrame read = nullptr;
    PFN_niteUserTrackerFrameRelease release = nullptr;
};

NiteSession::~NiteSession() { close(); }

bool NiteSession::open(const NiteLocation &loc, std::string &err, bool wantColor) {
    close();
    if (!loc.found) { err = loc.problem; return false; }
    if (!openni2Init(err)) return false;
    const int devs = openni2DeviceCount();
    logLine("OpenNI2 " + openni2Version() + ": " + std::to_string(devs) + " câmera(s) vista(s) pelo driver");

    // O NiTE fica carregado até o fim do processo (descarregar bibliotecas com threads é arriscado).
    const std::string libPath = loc.dir + "/libNiTE2.so";
    lib_ = dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib_) {
        const char *e = dlerror();
        err = std::string("Não foi possível carregar o NiTE2: ") + (e ? e : "erro desconhecido");
        openni2Shutdown();
        return false;
    }
    api_ = new Api;
    auto sym = [&](const char *name) { return dlsym(lib_, name); };
    api_->initialize = reinterpret_cast<PFN_niteInitialize>(sym("niteInitialize"));
    api_->shutdown = reinterpret_cast<PFN_niteShutdown>(sym("niteShutdown"));
    api_->getVersion = reinterpret_cast<PFN_niteGetVersion>(sym("niteGetVersion"));
    api_->initTracker = reinterpret_cast<PFN_niteInitializeUserTracker>(sym("niteInitializeUserTracker"));
    api_->shutdownTracker = reinterpret_cast<PFN_niteShutdownUserTracker>(sym("niteShutdownUserTracker"));
    api_->startSkeleton = reinterpret_cast<PFN_niteStartSkeletonTracking>(sym("niteStartSkeletonTracking"));
    api_->smoothing = reinterpret_cast<PFN_niteSetSkeletonSmoothing>(sym("niteSetSkeletonSmoothing"));
    api_->read = reinterpret_cast<PFN_niteReadUserTrackerFrame>(sym("niteReadUserTrackerFrame"));
    api_->release = reinterpret_cast<PFN_niteUserTrackerFrameRelease>(sym("niteUserTrackerFrameRelease"));
    if (!api_->initialize || !api_->shutdown || !api_->getVersion || !api_->initTracker || !api_->shutdownTracker ||
        !api_->startSkeleton || !api_->read || !api_->release) {
        err = "Este libNiTE2.so não tem as funções esperadas do NiTE 2 (versão errada?)";
        delete api_; api_ = nullptr;
        openni2Shutdown();
        return false;
    }
    const NiteVersion v = api_->getVersion();
    version_ = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.maintenance) + "." +
               std::to_string(v.build);
    if (v.major != 2 || v.minor < 0 || v.minor > 9) {
        err = "Versão do NiTE não suportada: " + version_ + " (é preciso o NiTE 2.x)";
        delete api_; api_ = nullptr;
        openni2Shutdown();
        return false;
    }
    logLine("NiTE " + version_ + " carregado de " + loc.dir);

    // O NiTE procura os dados em "./NiTE2" relativo à pasta atual: entra na pasta durante a inicialização.
    char oldCwd[4096];
    const bool haveCwd = getcwd(oldCwd, sizeof(oldCwd)) != nullptr;
    if (chdir(loc.dir.c_str()) != 0) logLine("aviso: não consegui entrar em " + loc.dir);
    NiteUserTrackerHandle h = nullptr;
    NiteStatus rc = api_->initialize();
    if (rc == NITE_STATUS_OK) rc = api_->initTracker(&h);
    if (haveCwd && chdir(oldCwd) != 0) logLine("aviso: não consegui voltar para " + std::string(oldCwd));
    if (rc != NITE_STATUS_OK || !h) {
        err = devs == 0 ? "O OpenNI2 não encontrou o Kinect (driver Freenect). Confira a fonte de 12 V, o cabo e a regra udev."
                        : std::string("O NiTE2 não conseguiu iniciar o rastreador de usuários. ") + oniGetExtendedError();
        api_->shutdown();
        delete api_; api_ = nullptr;
        openni2Shutdown();
        return false;
    }
    tracker_ = h;
    if (api_->smoothing) api_->smoothing(h, 0.3f);

    // Câmera RGB (opcional): mesmo dispositivo que o NiTE abriu.
    if (wantColor) {
        OniDeviceHandle dev = nullptr;
        if (oniDeviceOpen(nullptr, &dev) == ONI_STATUS_OK && dev) {
            OniStreamHandle cs = nullptr;
            if (oniDeviceCreateStream(dev, ONI_SENSOR_COLOR, &cs) == ONI_STATUS_OK && cs) {
                OniVideoMode m{ONI_PIXEL_FORMAT_RGB888, 640, 480, 30};
                oniStreamSetProperty(cs, ONI_STREAM_PROPERTY_VIDEO_MODE, &m, sizeof(m));
                if (oniStreamStart(cs) == ONI_STATUS_OK) {
                    colorDev_ = dev;
                    colorStream_ = cs;
                } else {
                    oniStreamDestroy(cs);
                    oniDeviceClose(dev);
                }
            } else {
                oniDeviceClose(dev);
            }
        }
        if (!colorStream_) logLine("câmera RGB indisponível pelo OpenNI2 (segue só com profundidade)");
    }

    openedNs_ = nowNs();
    lastFrameNs_ = 0;
    frames_ = 0;
    incompatible_ = false;
    mirrored_ = false;
    running_ = true;
    readDone_ = false;
    th_ = std::thread(&NiteSession::readLoop, this);
    if (colorStream_) {
        colorDone_ = false;
        colorTh_ = std::thread(&NiteSession::colorLoop, this);
    }
    return true;
}

void NiteSession::close() {
    if (!tracker_ && !api_) return;
    running_ = false;
    // Espera as threads; se a leitura do NiTE ficar presa (Kinect arrancado), não trava o programa.
    const int64_t t0 = nowNs();
    while ((!readDone_ || !colorDone_) && nowNs() - t0 < 2'500'000'000LL) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const bool clean = readDone_ && colorDone_;
    if (th_.joinable()) { if (readDone_) th_.join(); else th_.detach(); }
    if (colorTh_.joinable()) { if (colorDone_) colorTh_.join(); else colorTh_.detach(); }
    if (!clean) {
        logLine("a leitura não respondeu ao fechar; recursos do NiTE ficam abertos até o programa sair");
        tracker_ = nullptr;
        api_ = nullptr;  // vazado de propósito: a thread presa ainda pode usá-lo
        colorStream_ = colorDev_ = nullptr;
        return;
    }
    if (colorStream_) {
        oniStreamStop(static_cast<OniStreamHandle>(colorStream_));
        oniStreamDestroy(static_cast<OniStreamHandle>(colorStream_));
        colorStream_ = nullptr;
    }
    if (colorDev_) { oniDeviceClose(static_cast<OniDeviceHandle>(colorDev_)); colorDev_ = nullptr; }
    if (api_) {
        if (tracker_) api_->shutdownTracker(static_cast<NiteUserTrackerHandle>(tracker_));
        api_->shutdown();
        delete api_;
        api_ = nullptr;
        openni2Shutdown();
    }
    tracker_ = nullptr;
    std::lock_guard<std::mutex> lk(mu_);
    latest_ = Snapshot{};
}

bool NiteSession::deviceLost() const {
    if (!tracker_) return false;
    if (incompatible_) return true;
    const int64_t now = nowNs();
    if (frames_ == 0) return now - openedNs_ > 10'000'000'000LL;
    return now - lastFrameNs_ > 2'500'000'000LL;
}

bool NiteSession::fetch(Snapshot &out, uint64_t afterSeq) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (latest_.seq == 0 || latest_.seq <= afterSeq) return false;
    out.seq = latest_.seq;
    out.depthMm = latest_.depthMm;
    out.userMap = latest_.userMap;
    out.intr = latest_.intr;
    out.bodies = latest_.bodies;
    out.bodies.userMap = out.userMap.empty() ? nullptr : out.userMap.data();
    out.recvTime = latest_.recvTime;
    return true;
}

bool NiteSession::copyRgb(std::vector<uint8_t> &rgb, int &w, int &h) const {
    std::lock_guard<std::mutex> lk(rgbMu_);
    if (rgb_.empty() || nowNs() - rgbNs_ > 1'000'000'000LL) return false;
    rgb = rgb_;
    w = rgbW_;
    h = rgbH_;
    return true;
}

void NiteSession::colorLoop() {
    OniStreamHandle cs = static_cast<OniStreamHandle>(colorStream_);
    while (running_) {
        int idx = -1;
        if (oniWaitForAnyStream(&cs, 1, &idx, 200) != ONI_STATUS_OK) continue;
        OniFrame *f = nullptr;
        if (oniStreamReadFrame(cs, &f) != ONI_STATUS_OK || !f) continue;
        if (f->videoMode.pixelFormat == ONI_PIXEL_FORMAT_RGB888 && f->width > 0 && f->height > 0 &&
            f->dataSize >= f->width * f->height * 3) {
            std::lock_guard<std::mutex> lk(rgbMu_);
            rgbW_ = f->width;
            rgbH_ = f->height;
            rgb_.resize(static_cast<size_t>(f->width) * f->height * 3);
            for (int y = 0; y < f->height; ++y)
                std::memcpy(&rgb_[static_cast<size_t>(y) * f->width * 3],
                            static_cast<const uint8_t *>(f->data) + static_cast<size_t>(y) * f->stride, f->width * 3);
            rgbNs_ = nowNs();
        }
        oniFrameRelease(f);
    }
    colorDone_ = true;
}

void NiteSession::readLoop() {
    auto h = static_cast<NiteUserTrackerHandle>(tracker_);
    Api *api = api_;
    int lastIndex = -1, errors = 0, bad = 0;
    int mirrorVotes = 0, directVotes = 0;
    std::vector<int> skeletonAsked;
    Snapshot work;
    while (running_) {
        NiteUserTrackerFrame *f = nullptr;
        const NiteStatus rc = api->read(h, &f);
        if (rc != NITE_STATUS_OK || !f) {
            if (++errors % 100 == 1) logLine("falha ao ler quadro do NiTE (" + std::to_string(rc) + ")");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        errors = 0;
        if (f->frameIndex == lastIndex) {
            api->release(h, f);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            continue;
        }
        lastIndex = f->frameIndex;

        // --- conferências: se a estrutura não bate com o que esperamos, não confia no NiTE
        const OniFrame *df = f->pDepthFrame;
        bool ok = df && df->data && df->width >= 160 && df->width <= 1280 && df->height >= 120 && df->height <= 1024 &&
                  df->stride >= df->width * 2 && f->userCount >= 0 && f->userCount <= 32 &&
                  (f->userCount == 0 || f->pUser) && f->userMap.pixels && f->userMap.width == df->width &&
                  f->userMap.height == df->height && f->userMap.stride >= df->width * 2;
        for (int i = 0; ok && i < f->userCount; ++i) {
            const NiteUserData &u = f->pUser[i];
            if (u.id <= 0) ok = false;
            for (int j = 0; ok && j < NITE_JOINT_COUNT; ++j) {
                const NitePoint3f &p = u.skeleton.joints[j].position;
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || std::fabs(p.x) > 20000 ||
                    std::fabs(p.y) > 20000 || std::fabs(p.z) > 20000)
                    ok = false;
            }
        }
        if (!ok) {
            api->release(h, f);
            if (++bad == 30) {
                logLine("os quadros do NiTE não têm o formato esperado: desligando o modo NiTE2");
                incompatible_ = true;
            }
            continue;
        }
        bad = 0;

        const int W = df->width, H = df->height;
        work.intr = intrinsicsFor(W, H);
        work.depthMm.resize(static_cast<size_t>(W) * H);
        work.userMap.resize(static_cast<size_t>(W) * H);
        // Se a imagem vier espelhada, desespelha profundidade e rótulos: o jogo sempre vê o mundo real
        // (e as articulações do NiTE, que estão no mundo real, continuam batendo com os pixels).
        const bool mir = mirrored_;
        for (int y = 0; y < H; ++y) {
            const auto *dr = reinterpret_cast<const uint16_t *>(static_cast<const uint8_t *>(df->data) + static_cast<size_t>(y) * df->stride);
            const auto *um = reinterpret_cast<const int16_t *>(reinterpret_cast<const uint8_t *>(f->userMap.pixels) +
                                                               static_cast<size_t>(y) * f->userMap.stride);
            uint16_t *dd = &work.depthMm[static_cast<size_t>(y) * W];
            uint16_t *ud = &work.userMap[static_cast<size_t>(y) * W];
            if (!mir) {
                std::memcpy(dd, dr, W * 2);
                for (int x = 0; x < W; ++x) ud[x] = um[x] > 0 ? static_cast<uint16_t>(um[x]) : 0;
            } else {
                for (int x = 0; x < W; ++x) {
                    dd[W - 1 - x] = dr[x];
                    ud[W - 1 - x] = um[x] > 0 ? static_cast<uint16_t>(um[x]) : 0;
                }
            }
        }

        // --- usuários e esqueletos (mm, Y para cima) -> metros, Y para baixo
        const Intrinsics &in = work.intr;
        work.bodies.users.clear();
        for (int i = 0; i < f->userCount; ++i) {
            const NiteUserData &u = f->pUser[i];
            if (u.state & NITE_USER_STATE_LOST) continue;
            if (u.skeleton.state == NITE_SKELETON_NONE &&
                std::find(skeletonAsked.begin(), skeletonAsked.end(), u.id) == skeletonAsked.end()) {
                api->startSkeleton(h, u.id);
                skeletonAsked.push_back(u.id);
                logLine("usuário " + std::to_string(u.id) + " visto: calibrando esqueleto");
            }
            if (!(u.state & NITE_USER_STATE_VISIBLE)) continue;
            ExternalUser eu;
            eu.id = u.id;
            eu.skeleton = u.skeleton.state == NITE_SKELETON_TRACKED;
            // Espelhamento: a cabeça projetada deve cair sobre o próprio usuário no mapa
            if (eu.skeleton && u.skeleton.joints[NITE_JOINT_HEAD].positionConfidence >= 0.5f) {
                const NitePoint3f &p = u.skeleton.joints[NITE_JOINT_HEAD].position;
                if (p.z > 300) {
                    const int px = static_cast<int>(in.cx + p.x * in.fx / p.z);
                    const int py = static_cast<int>(in.cy - p.y * in.fy / p.z);
                    // compara no mapa ORIGINAL do NiTE (antes de desespelhar)
                    auto at = [&](int x, int y) {
                        if (mir) x = W - 1 - x;
                        return x >= 0 && y >= 0 && x < W && y < H && work.userMap[static_cast<size_t>(y) * W + x] == u.id;
                    };
                    const bool direct = at(px, py), mir = at(W - 1 - px, py);
                    if (direct && !mir) directVotes = std::min(directVotes + 1, 60);
                    if (mir && !direct) mirrorVotes = std::min(mirrorVotes + 1, 60);
                    if (!mirrored_ && mirrorVotes > 15 && mirrorVotes > 3 * directVotes) {
                        mirrored_ = true;
                        logLine("imagem espelhada detectada: ajustando o lado das articulações");
                    } else if (mirrored_ && directVotes > 15 && directVotes > 3 * mirrorVotes) {
                        mirrored_ = false;
                    }
                }
            }
            const float sx = 0.001f;
            for (int j = 0; j < NITE_JOINT_COUNT; ++j) {
                const NiteSkeletonJoint &jt = u.skeleton.joints[j];
                eu.joints[j] = V3(jt.position.x * sx, -jt.position.y * 0.001f, jt.position.z * 0.001f);
                eu.conf[j] = eu.skeleton ? std::clamp(jt.positionConfidence, 0.f, 1.f) : 0.f;
            }
            work.bodies.users.push_back(eu);
        }
        work.bodies.floorValid = f->floorConfidence > 0.5f && std::isfinite(f->floor.normal.y);
        if (work.bodies.floorValid) {
            const float sx = 1.f;
            work.bodies.floorPoint = V3(f->floor.point.x * 0.001f * sx, -f->floor.point.y * 0.001f, f->floor.point.z * 0.001f);
            work.bodies.floorNormal = V3(f->floor.normal.x * sx, -f->floor.normal.y, f->floor.normal.z).norm();
        }
        api->release(h, f);

        work.recvTime = nowNs() * 1e-9;
        {
            std::lock_guard<std::mutex> lk(mu_);
            work.seq = latest_.seq + 1;
            std::swap(latest_.depthMm, work.depthMm);
            std::swap(latest_.userMap, work.userMap);
            latest_.intr = work.intr;
            latest_.bodies = work.bodies;
            latest_.bodies.userMap = latest_.userMap.data();
            latest_.recvTime = work.recvTime;
            latest_.seq = work.seq;
        }
        lastFrameNs_ = nowNs();
        ++frames_;
    }
    readDone_ = true;
}

// ----------------------------------------------------------------------- feed

bool NiteFeed::fetch(std::vector<uint16_t> &mm, Intrinsics &intr) {
    if (!s_ || !s_->fetch(snap_, snap_.seq)) return false;
    mm = snap_.depthMm;
    intr = snap_.intr;
    haveRgb_ = s_->copyRgb(rgb_, rgbW_, rgbH_) && rgbW_ == 640 && rgbH_ == 480;
    return true;
}

// Profundidade -> RGB aproximado (calibração típica do Kinect v1: câmera RGB ~2 cm ao lado da IR).
// Serve só para colorir a silhueta; o modo libfreenect usa a calibração de fábrica do aparelho.
bool NiteFeed::color(int u, int v, uint16_t mm, uint8_t out[3]) const {
    if (!haveRgb_ || !mm) return false;
    const float z = mm * 0.001f;
    const float x = (u - snap_.intr.cx) * z / snap_.intr.fx;
    const float y = (v - snap_.intr.cy) * z / snap_.intr.fy;
    const float zr = z - 0.011f;
    if (zr <= 0.1f) return false;
    const int ru = static_cast<int>(328.9f + (x + 0.020f) * 529.2f / zr);
    const int rv = static_cast<int>(267.5f + (y - 0.0007f) * 525.6f / zr);
    if (ru < 0 || rv < 0 || ru >= rgbW_ || rv >= rgbH_) return false;
    const uint8_t *p = &rgb_[(static_cast<size_t>(rv) * rgbW_ + ru) * 3];
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    return true;
}

} // namespace k3d
