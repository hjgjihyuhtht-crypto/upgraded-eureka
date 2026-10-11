// NiTE2 FALSO — usado SOMENTE pelos testes automáticos (nite_test).
//
// Implementa a mesma API C que o programa carrega de libNiTE2.so, gerando quadros sintéticos com o
// boneco do simulador (SimDepth): profundidade, mapa de usuários, esqueleto e chão. Serve para testar
// o caminho completo (dlopen, troca de pasta, thread de leitura, conversão de coordenadas,
// espelhamento e o rastreador com esqueleto) sem o NiTE proprietário. NÃO é distribuído no pacote.
//
// Variáveis de ambiente: FAKE_NITE_MIRROR=1 (imagem espelhada), FAKE_NITE_GARBAGE=1 (quadros inválidos).
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <vector>

#include "body/sim_depth.h"
#include "kinect/nite_capi.h"

using namespace k3d;

struct NiteUserTracker {
    SimDepth sim;
    SimScene scene;
    std::vector<uint16_t> depth, empty;
    std::vector<NiteUserId> map;
    OniFrame depthFrame{};
    NiteUserData user{};
    NiteUserTrackerFrame frame{};
    int index = 0;
    bool skeletonStarted = false;
    int skeletonFrames = 0;
    bool mirror = false, garbage = false;
    std::chrono::steady_clock::time_point next;
};

namespace {
bool gInit = false;

// Plano de jogo -> coordenadas do mundo do OpenNI (mm, Y para cima), como o NiTE entregaria.
NitePoint3f toOni(const SimScene &sc, V2 g, float z) {
    const float th = sc.pitchDeg * 3.14159265f / 180.f, ct = std::cos(th), st = std::sin(th);
    const V3 w(-g.x, g.y - sc.cameraHeight, z);
    const float cy = w.y * ct - w.z * st, cz = w.y * st + w.z * ct;
    return {w.x * 1000.f, cy * 1000.f, cz * 1000.f};
}

// Roteiro: sem ninguém, depois uma pessoa em x = +0,3 m que levanta a mão direita e depois pula.
SimPose poseAt(int frame, bool &present) {
    SimPose p;
    present = frame >= 10;
    p.x = 0.3f;
    p.z = 2.5f;
    p.handsAuto = true;
    if (frame >= 60 && frame < 140) {
        p.handsAuto = false;
        p.rightHand = {p.x + 0.25f, 2.05f};
        p.leftHand = {p.x - 0.30f, 0.80f};
    }
    if (frame >= 150 && frame < 165) p.jump = 0.25f * std::sin((frame - 150) / 15.f * 3.14159f);
    return p;
}
} // namespace

extern "C" {

__attribute__((visibility("default"))) NiteStatus niteInitialize() { gInit = true; return NITE_STATUS_OK; }
__attribute__((visibility("default"))) void niteShutdown() { gInit = false; }
__attribute__((visibility("default"))) NiteVersion niteGetVersion() { return NiteVersion{2, 2, 0, 11}; }

__attribute__((visibility("default"))) NiteStatus niteInitializeUserTracker(NiteUserTrackerHandle *out) {
    if (!gInit || !out) return NITE_STATUS_ERROR;
    // como o NiTE real: os dados ficam em ./NiTE2 (relativo à pasta atual)
    if (access("NiTE2/s.dat", R_OK) != 0) return NITE_STATUS_ERROR;
    auto *t = new NiteUserTracker;
    t->scene.noise = false;
    t->scene.fast = true;
    t->scene.intr.fx = 320.f / std::tan(58.5f * 3.14159265f / 360.f);
    t->scene.intr.fy = 240.f / std::tan(45.6f * 3.14159265f / 360.f);
    const char *m = std::getenv("FAKE_NITE_MIRROR");
    const char *g = std::getenv("FAKE_NITE_GARBAGE");
    t->mirror = m && *m == '1';
    t->garbage = g && *g == '1';
    t->next = std::chrono::steady_clock::now();
    *out = t;
    return NITE_STATUS_OK;
}

__attribute__((visibility("default"))) NiteStatus niteShutdownUserTracker(NiteUserTrackerHandle t) { delete t; return NITE_STATUS_OK; }

__attribute__((visibility("default"))) NiteStatus niteStartSkeletonTracking(NiteUserTrackerHandle t, NiteUserId id) {
    if (id != 1) return NITE_STATUS_BAD_USER_ID;
    t->skeletonStarted = true;
    return NITE_STATUS_OK;
}
__attribute__((visibility("default"))) void niteStopSkeletonTracking(NiteUserTrackerHandle t, NiteUserId) { t->skeletonStarted = false; }
__attribute__((visibility("default"))) NiteStatus niteSetSkeletonSmoothing(NiteUserTrackerHandle, float) { return NITE_STATUS_OK; }
__attribute__((visibility("default"))) NiteStatus niteUserTrackerFrameRelease(NiteUserTrackerHandle, NiteUserTrackerFrame *) { return NITE_STATUS_OK; }

__attribute__((visibility("default"))) NiteStatus niteReadUserTrackerFrame(NiteUserTrackerHandle t, NiteUserTrackerFrame **out) {
    // bloqueia até o próximo quadro (30 Hz), como o NiTE real
    t->next += std::chrono::milliseconds(33);
    std::this_thread::sleep_until(t->next);
    const int fi = t->index++;
    bool present = false;
    SimPose pose = poseAt(fi, present);
    pose.present = present;
    t->sim.render(t->scene, &pose, present ? 1 : 0, t->depth);
    t->sim.render(t->scene, nullptr, 0, t->empty);
    const int W = 640, H = 480;
    t->map.assign(W * H, 0);
    for (int i = 0; i < W * H; ++i)
        if (present && t->depth[i] && t->depth[i] + 5 < t->empty[i]) t->map[i] = 1;
    if (t->mirror)
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W / 2; ++x) {
                std::swap(t->depth[y * W + x], t->depth[y * W + W - 1 - x]);
                std::swap(t->map[y * W + x], t->map[y * W + W - 1 - x]);
            }

    OniFrame &df = t->depthFrame;
    df = OniFrame{};
    df.data = t->depth.data();
    df.dataSize = W * H * 2;
    df.width = W;
    df.height = H;
    df.stride = W * 2;
    df.frameIndex = fi;
    df.sensorType = ONI_SENSOR_DEPTH;
    df.videoMode = OniVideoMode{ONI_PIXEL_FORMAT_DEPTH_1_MM, W, H, 30};

    NiteUserTrackerFrame &f = t->frame;
    f = NiteUserTrackerFrame{};
    f.pDepthFrame = &df;
    f.frameIndex = fi;
    f.timestamp = static_cast<unsigned long long>(fi) * 33333ULL;
    f.userMap = NiteUserMap{t->map.data(), t->garbage ? W / 2 : W, H, W * 2};
    // chão: y = 0 no mundo do simulador
    f.floorConfidence = 0.9f;
    {
        const SimScene &sc = t->scene;
        const float th = sc.pitchDeg * 3.14159265f / 180.f;
        f.floor.point = toOni(sc, {0, 0}, 2.0f);
        f.floor.normal = {0, std::cos(th), std::sin(th)};
    }
    if (present) {
        NiteUserData &u = t->user;
        const bool first = !(u.id == 1);
        u = NiteUserData{};
        u.id = 1;
        u.state = NITE_USER_STATE_VISIBLE | (first ? NITE_USER_STATE_NEW : 0);
        if (t->skeletonStarted) ++t->skeletonFrames;
        u.skeleton.state = !t->skeletonStarted ? NITE_SKELETON_NONE
                           : t->skeletonFrames < 10 ? NITE_SKELETON_CALIBRATING : NITE_SKELETON_TRACKED;
        const SimDepth::Skeleton k = SimDepth::skeleton(pose);
        const float s = pose.height / 1.72f;
        const float hipY = pose.jump + 0.90f * s * (1.f - 0.40f * pose.crouch);
        const float z = pose.z;
        // Nomes do ponto de vista da CÂMERA (como o NiTE sem espelho): "LEFT" = mão direita do jogador.
        V2 pts[NITE_JOINT_COUNT];
        pts[NITE_JOINT_HEAD] = k.head;
        pts[NITE_JOINT_NECK] = {pose.x, hipY + 0.56f * s};
        pts[NITE_JOINT_LEFT_SHOULDER] = k.rightShoulder;
        pts[NITE_JOINT_RIGHT_SHOULDER] = k.leftShoulder;
        pts[NITE_JOINT_LEFT_ELBOW] = (k.rightShoulder + k.rightHand) * 0.5f;
        pts[NITE_JOINT_RIGHT_ELBOW] = (k.leftShoulder + k.leftHand) * 0.5f;
        pts[NITE_JOINT_LEFT_HAND] = k.rightHand;
        pts[NITE_JOINT_RIGHT_HAND] = k.leftHand;
        pts[NITE_JOINT_TORSO] = {pose.x, hipY + 0.28f * s};
        pts[NITE_JOINT_LEFT_HIP] = {pose.x + 0.10f * s, hipY};
        pts[NITE_JOINT_RIGHT_HIP] = {pose.x - 0.10f * s, hipY};
        pts[NITE_JOINT_LEFT_KNEE] = {pose.x + 0.10f * s, hipY * 0.5f + pose.jump * 0.5f};
        pts[NITE_JOINT_RIGHT_KNEE] = {pose.x - 0.10f * s, hipY * 0.5f + pose.jump * 0.5f};
        pts[NITE_JOINT_LEFT_FOOT] = {pose.x + 0.10f * s, pose.jump + 0.05f};
        pts[NITE_JOINT_RIGHT_FOOT] = {pose.x - 0.10f * s, pose.jump + 0.05f};
        for (int j = 0; j < NITE_JOINT_COUNT; ++j) {
            NiteSkeletonJoint &jt = u.skeleton.joints[j];
            jt.jointType = j;
            jt.position = toOni(t->scene, pts[j], z);
            jt.positionConfidence = u.skeleton.state == NITE_SKELETON_TRACKED ? 1.f : 0.f;
            jt.orientation = {0, 0, 0, 1};
        }
        u.centerOfMass = toOni(t->scene, pts[NITE_JOINT_TORSO], z);
        f.userCount = 1;
        f.pUser = &t->user;
    }
    *out = &f;
    return NITE_STATUS_OK;
}

} // extern "C"
