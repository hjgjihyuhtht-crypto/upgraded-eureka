// Modo aventura: medalhas, desbloqueio, emblemas e perfil salvo.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "game/adventure.h"

using namespace k3d::game;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static Result res(GameId g, int score, int max, float secs = 60) {
    Result r;
    r.game = g;
    r.score = score;
    r.maxScore = max;
    r.medal = medalFor(score, max);
    r.seconds = secs;
    return r;
}

int main() {
    // medalhas
    CHECK(medalFor(0, 10) == Medal::None);
    CHECK(medalFor(3, 10) == Medal::Bronze);
    CHECK(medalFor(5, 10) == Medal::Silver);
    CHECK(medalFor(7, 10) == Medal::Gold);
    CHECK(medalFor(9, 10) == Medal::Platinum);
    CHECK(medalFor(12, 10) == Medal::Platinum);

    // estrutura: 15 aventuras, cada jogo em cada dificuldade exatamente uma vez
    CHECK(adventures().size() == 15 && expeditions().size() == 5);
    int seen[kGameCount][3] = {};
    for (auto &a : adventures()) seen[static_cast<int>(a.game)][a.difficulty]++;
    for (auto &g : seen) for (int c : g) CHECK(c == 1);
    CHECK(emblems().size() == 20);

    Profile p;
    CHECK(p.expeditionUnlocked(0) && !p.expeditionUnlocked(1));
    auto e1 = p.record(res(GameId::River, 2, 10), 0);
    CHECK(p.bestMedal[0] == 0);
    CHECK(e1.size() == 1 && std::string(e1[0]->id) == "primeira");
    p.record(res(GameId::River, 8, 10), 0);
    p.record(res(GameId::Rebound, 4, 10), 1);
    CHECK(!p.expeditionUnlocked(1));
    auto e2 = p.record(res(GameId::Leaks, 9, 10), 2);
    CHECK(p.expeditionUnlocked(1) && !p.expeditionUnlocked(2));
    bool exp1 = false, plat = false;
    for (auto *e : e2) { exp1 |= std::string(e->id) == "exp1"; plat |= std::string(e->id) == "platina"; }
    CHECK(exp1 && plat);
    // a melhor medalha não piora
    p.record(res(GameId::River, 1, 10), 0);
    CHECK(p.bestMedal[0] == static_cast<int>(Medal::Gold));

    // emblemas por estatísticas da partida
    Result r = res(GameId::Rebound, 10, 10);
    r.stats["maxTargetsOneBall"] = 3;
    r.stats["cleared"] = 1;
    r.playersSeen = 2;
    auto e3 = p.record(r, -1);
    CHECK(p.hasEmblem("triplo") && p.hasEmblem("paredao") && p.hasEmblem("dupla"));
    CHECK(e3.size() == 3);
    CHECK(p.freeBest[static_cast<int>(GameId::Rebound)][0] == 10);
    p.recordPhotos(20);
    CHECK(p.hasEmblem("fotografo"));

    // salvar e carregar (inclui nome com acento)
    p.name = "Joana Conceição";
    const std::string path = "/tmp/k3d_profile_test.txt";
    CHECK(p.save(path));
    Profile q;
    CHECK(q.load(path));
    CHECK(q.name == p.name && q.bestMedal == p.bestMedal && q.bestScore == p.bestScore);
    CHECK(q.emblemsEarned == p.emblemsEarned && q.gamesPlayed == p.gamesPlayed && q.photosTaken == 20);
    CHECK(q.freeBest[1][0] == 10);
    // arquivo corrompido não derruba o carregamento
    {
        FILE *f = std::fopen(path.c_str(), "ab");
        std::fputs("aventura.3=lixo\npartidas=abc\nemblema=\n", f);
        std::fclose(f);
    }
    Profile z;
    CHECK(z.load(path) && z.gamesPlayed == p.gamesPlayed);
    CHECK(!Profile().load("/tmp/nao/existe/perfil.txt"));
    std::printf("OK\n");
    return 0;
}
