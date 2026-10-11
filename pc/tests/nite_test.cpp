// Testes do modo OpenNI2 + NiTE2 (Linux).
//
// 1. libOpenNI2 (compilado aqui) inicia e carrega o driver Freenect de OpenNI2/Drivers;
// 2. procura do NiTE: mensagens quando falta a biblioteca ou a pasta de dados;
// 3. caminho completo com um NiTE FALSO (tests/fake_nite): dlopen, pasta de dados, thread de leitura,
//    conversão de coordenadas, espelhamento e o rastreador usando rótulos + esqueleto;
// 4. quadros com formato inesperado desligam o modo NiTE (o App volta para o rastreador próprio).
//
// O NiTE real é proprietário e não pode ser testado aqui; a estrutura dos quadros dele é conferida
// em tempo de execução (veja NiteSession::readLoop).
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "body/body_tracker.h"
#include "kinect/nite_tracker.h"
#include "kinect/pc_session.h"

using namespace k3d;
namespace fs = std::filesystem;

static int fails = 0;
#define CHECK(c, ...)                                                    \
    do {                                                                 \
        if (!(c)) { ++fails; std::printf("FALHOU %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); } \
    } while (0)

struct RunStats {
    int frames = 0, withPlayer = 0, withSkeleton = 0, handUpOk = 0, handUpFrames = 0, jumps = 0;
    float x = 0, floorH = 0;
    bool mirrored = false;
};

static RunStats runSession(const NiteLocation &loc, double seconds) {
    RunStats r;
    NiteSession s;
    std::string err;
    char before[4096];
    (void)!getcwd(before, sizeof(before));
    const bool ok = s.open(loc, err, true);
    CHECK(ok, "%s", err.c_str());
    char after[4096];
    (void)!getcwd(after, sizeof(after));
    CHECK(std::string(before) == after, "a pasta atual não foi restaurada: %s", after);
    if (!ok) return r;
    NiteFeed feed(&s);
    BodyTracker tracker;
    std::vector<uint16_t> mm;
    Intrinsics intr;
    const auto t0 = std::chrono::steady_clock::now();
    double t = 0;
    while ((t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()) < seconds) {
        if (!feed.fetch(mm, intr)) { std::this_thread::sleep_for(std::chrono::milliseconds(4)); continue; }
        const BodyFrame &f = tracker.update(mm.data(), intr, t, feed.color(), feed.external());
        ++r.frames;
        CHECK(f.skeletonSource, "quadro sem a marca de NiTE");
        const PlayerBody *p = f.first();
        if (!p) continue;
        ++r.withPlayer;
        r.x = p->x;
        r.floorH = f.cameraHeight;
        if (p->jumpEvent) ++r.jumps;
        if (!p->skeleton) continue;
        ++r.withSkeleton;
        // roteiro do NiTE falso: quadros 60..140 com a mão DIREITA do jogador no alto
        const uint64_t fi = s.frames();
        if (fi > 75 && fi < 130) {
            ++r.handUpFrames;
            if (p->rightHand.y > 1.75f && p->rightHand.x > p->x + 0.1f && p->leftHand.y < 1.1f) ++r.handUpOk;
        }
    }
    r.mirrored = s.mirrored();
    CHECK(!s.deviceLost(), "sessão acusou perda do dispositivo");
    const auto c0 = std::chrono::steady_clock::now();
    s.close();
    const double closeS = std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count();
    CHECK(closeS < 1.0, "fechar demorou %.2f s", closeS);
    return r;
}

int main() {
    // O libusb (dentro do driver Freenect) precisa de /sys/bus/usb. Contêineres sem USB não têm:
    // aí só dá para testar a procura do NiTE.
    const bool usb = fs::exists("/sys/bus/usb/devices");
    if (!usb) std::printf("AVISO: sistema sem /sys/bus/usb; pulando os testes que carregam o driver Freenect\n");
    // ---------------------------------------------------------------- 1. OpenNI2
    if (usb) {
        std::string err;
        const bool ok = openni2Init(err);
        CHECK(ok, "OpenNI2 com o driver Freenect: %s", err.c_str());
        if (ok) {
            std::printf("OpenNI2 %s iniciou com o driver Freenect; câmeras vistas: %d\n", openni2Version().c_str(),
                        openni2DeviceCount());
        }
        // (fica iniciado; NiteSession usa contagem de referências)
    }

    // ------------------------------------------------------------ 2. procura
    const fs::path fake = fs::path(executableDir()) / "fake_nite";
    const fs::path tmpHome = fs::temp_directory_path() / ("k3d_nite_home_" + std::to_string(getpid()));
    fs::create_directories(tmpHome);
    setenv("HOME", tmpHome.c_str(), 1);
    unsetenv("NITE2_REDIST64");
    unsetenv("NITE2_REDIST");
    unsetenv("NITE2_PATH");
    {
        const NiteLocation loc = locateNite((tmpHome / "dados").string());
        CHECK(!loc.found, "achou NiTE onde não há");
        CHECK(loc.problem.find("nite") != std::string::npos, "mensagem: %s", loc.problem.c_str());
        CHECK(loc.searched.size() >= 2, "procurou em %zu lugares", loc.searched.size());
    }
    fs::remove_all(fake / "NiTE2");
    setenv("NITE2_PATH", fake.c_str(), 1);
    {
        const NiteLocation loc = locateNite("");
        CHECK(!loc.found && loc.problem.find("NiTE2") != std::string::npos, "sem pasta de dados: %s", loc.problem.c_str());
    }
    fs::create_directories(fake / "NiTE2");
    std::ofstream(fake / "NiTE2" / "s.dat") << "dados de teste\n";
    const NiteLocation loc = locateNite("");
    CHECK(loc.found, "%s", loc.problem.c_str());
    if (!loc.found) { std::printf("%d falha(s)\n", fails); return 1; }
    if (!usb) {
        fs::remove_all(tmpHome);
        std::printf(fails ? "%d falha(s)\n" : "nite_test: procura ok (resto pulado)\n", fails);
        return fails ? 1 : 0;
    }

    // ---------------------------------------------- 3. caminho completo (falso)
    for (int mirror = 0; mirror < 2; ++mirror) {
        setenv("FAKE_NITE_MIRROR", mirror ? "1" : "0", 1);
        const RunStats r = runSession(loc, 6.0);
        std::printf("%s: %d quadros, %d com jogador, %d com esqueleto, mão no alto %d/%d, pulos %d, x=%.2f, "
                    "altura do sensor %.2f m, espelho detectado=%d\n",
                    mirror ? "espelhado" : "normal", r.frames, r.withPlayer, r.withSkeleton, r.handUpOk, r.handUpFrames,
                    r.jumps, r.x, r.floorH, r.mirrored);
        CHECK(r.frames > 120, "poucos quadros: %d", r.frames);
        CHECK(r.withPlayer > 100, "jogador visto em %d quadros", r.withPlayer);
        CHECK(r.withSkeleton > 80, "esqueleto em %d quadros", r.withSkeleton);
        CHECK(r.handUpFrames > 20 && r.handUpOk >= r.handUpFrames * 9 / 10, "mão direita no alto: %d/%d", r.handUpOk, r.handUpFrames);
        CHECK(std::fabs(r.x - 0.3f) < 0.12f, "posição x = %.2f (esperado +0,30: direita do jogador)", r.x);
        CHECK(std::fabs(r.floorH - 1.0f) < 0.06f, "altura do sensor = %.2f (esperado 1,00)", r.floorH);
        CHECK(r.jumps >= 1, "pulo não detectado");
        CHECK(r.mirrored == (mirror == 1), "espelhamento: detectado=%d esperado=%d", r.mirrored, mirror);
    }
    setenv("FAKE_NITE_MIRROR", "0", 1);

    // --------------------------------------- 4. quadros com formato inesperado
    {
        setenv("FAKE_NITE_GARBAGE", "1", 1);
        NiteSession s;
        std::string err;
        CHECK(s.open(loc, err, false), "%s", err.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        CHECK(s.incompatible() && s.deviceLost(), "quadros inválidos deveriam desligar o NiTE");
        s.close();
        unsetenv("FAKE_NITE_GARBAGE");
    }

    // registro (diagnóstico)
    const std::string log = PcKinectSession::drainLog();
    CHECK(log.find("NiTE 2.2.0.11") != std::string::npos, "log sem a versão do NiTE");
    std::printf("--- registro ---\n%s", log.c_str());

    fs::remove_all(tmpHome);
    if (fails) { std::printf("%d falha(s)\n", fails); return 1; }
    std::printf("nite_test: tudo certo\n");
    return 0;
}
