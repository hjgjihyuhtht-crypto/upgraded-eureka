// Testes do rastreador com profundidade sintética (simulação), incluindo ruído.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "body/body_tracker.h"
#include "body/sim_depth.h"

using namespace k3d;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
#define NEAR(a, b, tol) do { const double _a = (a), _b = (b); if (std::fabs(_a - _b) > (tol)) { \
    std::fprintf(stderr, "FALHOU %s:%d: %s = %.3f, esperado %.3f ± %.3f\n", __FILE__, __LINE__, #a, _a, _b, (double)(tol)); std::exit(1); } } while (0)

struct Rig {
    SimScene scene;
    SimDepth sim;
    BodyTracker tracker;
    std::vector<uint16_t> depth;
    double t = 0;
    const BodyFrame &step(const std::vector<SimPose> &poses) {
        sim.render(scene, poses.data(), static_cast<int>(poses.size()), depth);
        t += 1.0 / 30.0;
        return tracker.update(depth.data(), scene.intr, t);
    }
    const BodyFrame &hold(const SimPose &p, int frames) {
        const BodyFrame *f = nullptr;
        for (int i = 0; i < frames; ++i) f = &step({p});
        return *f;
    }
};

static SimPose absent() { SimPose p; p.present = false; return p; }

static void testFloorAndStanding() {
    // (inclinação, altura do sensor, distância) em que o corpo inteiro aparece
    const float setups[3][3] = {{0.f, 0.9f, 2.4f}, {-6.f, 1.0f, 3.0f}, {-15.f, 1.4f, 3.2f}};
    for (const auto &su : setups) {
        const float pitch = su[0];
        Rig r;
        r.scene.pitchDeg = pitch;
        r.scene.cameraHeight = su[1];
        SimPose p; p.x = 0.4f; p.z = su[2]; p.height = 1.75f;
        const BodyFrame &f = r.hold(p, 20);
        CHECK(f.floorFound);
        NEAR(f.cameraHeight, su[1], 0.05);
        CHECK(f.count() == 1);
        const PlayerBody &b = *f.first();
        NEAR(b.x, 0.4, 0.08);
        NEAR(b.z, su[2], 0.15);
        NEAR(b.standY, 1.75, 0.08);
        CHECK(!b.airborne && b.crouch < 0.15f && std::fabs(b.lean) < 0.12f);
        CHECK(b.guidance == G_NONE);
        std::printf("  inclinação %.0f°: chão %.2f m, x=%.2f z=%.2f altura=%.2f\n", pitch, f.cameraHeight, b.x, b.z, b.standY);
    }
    // cabeça fora do quadro: o aviso aparece
    Rig r;
    r.scene.pitchDeg = -6.f;
    SimPose p; p.z = 2.3f; p.height = 1.85f;
    CHECK(r.hold(p, 10).players[0].guidance & G_HEAD_CUT);
}

static void testJumpCrouchLean() {
    Rig r;
    SimPose p;
    r.hold(p, 40);
    // pulo: sobe e desce
    int jumpEvents = 0; float maxH = 0;
    for (int i = 0; i < 24; ++i) {
        const float tt = i / 23.f;
        p.jump = 0.35f * std::sin(tt * 3.14159f);
        const BodyFrame &f = r.step({p});
        jumpEvents += f.players[0].jumpEvent;
        maxH = std::max(maxH, f.players[0].jumpHeight);
    }
    p.jump = 0;
    r.hold(p, 10);
    CHECK(jumpEvents == 1);
    NEAR(maxH, 0.35, 0.12);

    // agachar
    p.crouch = 0.9f;
    const BodyFrame &fc = r.hold(p, 8);
    CHECK(fc.players[0].ducking && !fc.players[0].airborne);
    p.crouch = 0;
    CHECK(!r.hold(p, 6).players[0].ducking);

    // inclinar para a direita do jogador
    p.lean = 0.35f;
    const float leanR = r.hold(p, 8).players[0].lean;
    p.lean = -0.35f;
    const float leanL = r.hold(p, 8).players[0].lean;
    std::printf("  lean R=%.3f L=%.3f\n", leanR, leanL);
    CHECK(leanR > 0.15f && leanL < -0.15f);
    std::printf("  pulo %.2f m, inclinação direita %.2f / esquerda %.2f rad\n", maxH, leanR, leanL);
}

static void testHandsAndFlap() {
    Rig r;
    SimPose p;
    p.handsAuto = false;
    p.leftHand = {-0.75f, 1.35f};
    p.rightHand = {0.6f, 1.9f};
    const BodyFrame &f = r.hold(p, 15);
    const auto sk = SimDepth::skeleton(p);
    const PlayerBody &b = f.players[0];
    NEAR(b.rightHand.x, sk.rightHand.x, 0.12);
    NEAR(b.rightHand.y, sk.rightHand.y, 0.12);
    NEAR(b.leftHand.x, sk.leftHand.x, 0.12);
    NEAR(b.leftHand.y, sk.leftHand.y, 0.12);
    CHECK(!b.handsUp);

    p.leftHand = {-0.25f, 2.2f};
    p.rightHand = {0.25f, 2.2f};
    CHECK(r.hold(p, 8).players[0].handsUp);

    // batida de braços: de cima para baixo em ~0,2 s
    int flaps = 0;
    for (int rep = 0; rep < 3; ++rep) {
        for (int i = 0; i < 6; ++i) {
            const float y = 2.0f - i * 0.22f;
            p.leftHand = {-0.65f, y}; p.rightHand = {0.65f, y};
            flaps += r.step({p}).players[0].flapEvent;
        }
        for (int i = 0; i < 9; ++i) {
            const float y = 0.9f + i * 0.13f;
            p.leftHand = {-0.65f, y}; p.rightHand = {0.65f, y};
            flaps += r.step({p}).players[0].flapEvent;
        }
    }
    CHECK(flaps == 3);
    std::printf("  mão direita (%.2f, %.2f) esperado (%.2f, %.2f); %d batidas de braço\n",
                b.rightHand.x, b.rightHand.y, sk.rightHand.x, sk.rightHand.y, flaps);
}

static void testTwoPlayersAndGrid() {
    Rig r;
    SimPose a, b;
    a.x = -0.7f; b.x = 0.75f; b.height = 1.45f; b.z = 2.6f;
    const BodyFrame *f = nullptr;
    for (int i = 0; i < 15; ++i) f = &r.step({a, b});
    CHECK(f->count() == 2);
    const PlayerBody &p0 = f->players[0], &p1 = f->players[1];
    const PlayerBody &left = p0.x < p1.x ? p0 : p1, &right = p0.x < p1.x ? p1 : p0;
    NEAR(left.x, -0.7, 0.1);
    NEAR(right.x, 0.75, 0.1);
    NEAR(right.standY, 1.45, 0.08);
    // a grade do jogador da direita cobre o tronco dele e não o do outro
    CHECK(right.grid.overlapCircle({0.75f, 1.0f}, 0.15f).cells > 5);
    CHECK(right.grid.overlapCircle({-0.7f, 1.0f}, 0.15f).cells == 0);
    // vagas estáveis: trocar a ordem dos bonecos não troca os jogadores
    const int idLeft = left.trackId;
    for (int i = 0; i < 5; ++i) f = &r.step({b, a});
    const PlayerBody &l2 = f->players[0].x < f->players[1].x ? f->players[0] : f->players[1];
    CHECK(l2.trackId == idLeft);
    std::printf("  dois jogadores: x=%.2f e x=%.2f\n", left.x, right.x);
}

static void testGuidanceAndEmpty() {
    Rig r;
    CHECK(r.hold(absent(), 10).count() == 0);
    SimPose p; p.z = 1.0f;
    const BodyFrame &f = r.hold(p, 10);
    CHECK(f.count() == 1);
    CHECK(f.players[0].guidance & G_TOO_CLOSE);
    CHECK(f.players[0].guidance & G_HEAD_CUT);
    // sem pessoa a cena volta a ficar vazia
    CHECK(r.hold(absent(), 12).count() == 0);
}

int main() {
    std::printf("chão e pessoa em pé\n");    testFloorAndStanding();
    std::printf("pulo, agachar, inclinar\n"); testJumpCrouchLean();
    std::printf("mãos e braços\n");           testHandsAndFlap();
    std::printf("dois jogadores\n");          testTwoPlayersAndGrid();
    std::printf("orientações\n");             testGuidanceAndEmpty();
    std::printf("OK\n");
    return 0;
}
