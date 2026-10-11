// Joga cada minijogo do começo ao fim com profundidade sintética passando pelo
// rastreador real. Compara um "jogador robô" ativo com um jogador parado.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "body/body_tracker.h"
#include "body/sim_depth.h"
#include "game/autopilot.h"
#include "game/game_common.h"

using namespace k3d;
using namespace k3d::game;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

struct Run { Result r; int frames = 0; int photos = 0; };

static Run play(GameId id, int diff, bool active, int players, uint32_t seed = 7) {
    auto g = createGame(id);
    GameConfig cfg;
    cfg.difficulty = diff;
    cfg.seed = seed;
    cfg.players = players;
    g->start(cfg);
    SimScene scene;
    scene.noise = true;
    scene.fast = true;
    SimDepth sim;
    BodyTracker tracker;
    Autopilot bot;
    std::vector<uint16_t> depth;
    BodyFrame body;
    Events ev;
    Run run;
    double t = 0;
    const float dt = 1.f / 60.f;
    if (players == 2) { bot.pose[0].x = -0.6f; bot.pose[1].x = 0.6f; }
    for (int step = 0; step < 60 * 220 && !g->done(); ++step) {
        if (step % 2 == 0) {
            if (active) bot.update(2 * dt, *g, body, players);
            else for (int i = 0; i < 2; ++i) { bot.pose[i].present = i < players; }
            sim.render(scene, bot.pose, players, depth);
            body = tracker.update(depth.data(), scene.intr, t);
        }
        ev.clear();
        g->update(dt, body, ev);
        run.photos += static_cast<int>(ev.photos.size());
        t += dt;
        ++run.frames;
    }
    CHECK(g->done());
    run.r = g->result();
    return run;
}

int main(int argc, char **argv) {
    const bool quick = argc > 1 && std::strcmp(argv[1], "--rapido") == 0;
    std::printf("%-15s %-8s %8s %8s %6s %s\n", "jogo", "nível", "robô", "parado", "fotos", "medalha do robô");
    for (int gi = 0; gi < kGameCount; ++gi) {
        const GameId id = static_cast<GameId>(gi);
        for (int d = 0; d < 3; ++d) {
            if (quick && d != 1) continue;
            const Run a = play(id, d, true, 1);
            const Run b = play(id, d, false, 1);
            std::printf("%-15s %-8s %4d/%-3d %4d/%-3d %6d %s  (%.0f s)\n", gameName(id), difficultyName(d), a.r.score,
                        a.r.maxScore, b.r.score, b.r.maxScore, a.photos, medalName(a.r.medal), a.r.seconds);
            CHECK(a.r.maxScore > 0);
            CHECK(a.r.seconds > 20);
            // jogar de verdade rende bem mais do que ficar parado
            CHECK(a.r.score > b.r.score);
            CHECK(a.r.medal >= Medal::Bronze);
        }
    }
    // dois jogadores
    for (GameId id : {GameId::River, GameId::Rebound, GameId::Bubbles}) {
        const Run two = play(id, 0, true, 2);
        std::printf("dupla  %-15s %d/%d, jogadores vistos %d\n", gameName(id), two.r.score, two.r.maxScore, two.r.playersSeen);
        CHECK(two.r.playersSeen == 2);
        CHECK(two.r.medal >= Medal::Bronze);
    }
    // determinismo: mesma semente, mesmo resultado
    const Run x = play(GameId::Dodge, 1, true, 1, 99), y = play(GameId::Dodge, 1, true, 1, 99);
    CHECK(x.r.score == y.r.score && x.r.maxScore == y.r.maxScore);
    std::printf("OK\n");
    return 0;
}
