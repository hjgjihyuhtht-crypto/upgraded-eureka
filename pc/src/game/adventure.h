#pragma once
// Modo Aventura: expedições, medalhas, emblemas e perfil salvo em disco.
#include <array>
#include <string>
#include <vector>

#include "game/game_common.h"

namespace k3d::game {

struct Adventure {
    const char *name;
    GameId game;
    int difficulty;
    uint32_t seed;
};

struct Expedition {
    const char *name;
    std::array<int, 3> adventures;  // índices em adventures()
};

const std::vector<Adventure> &adventures();
const std::vector<Expedition> &expeditions();

struct Emblem {
    const char *id;
    const char *name;
    const char *description;
};
const std::vector<Emblem> &emblems();

struct Profile {
    std::string name = "Jogador";
    std::vector<int> bestMedal;    // por aventura (Medal como int)
    std::vector<int> bestScore;    // por aventura
    // melhor pontuação no jogo livre: [jogo][dificuldade]
    int freeBest[kGameCount][3] = {};
    std::vector<std::string> emblemsEarned;
    float secondsPlayed = 0;
    int gamesPlayed = 0;
    int totalPoints = 0;
    int totalJumps = 0;
    int photosTaken = 0;
    int duoGames = 0;

    Profile();
    bool expeditionUnlocked(int e) const;
    int medalsCount(Medal atLeast) const;
    bool hasEmblem(const std::string &id) const;

    // Registra uma partida. adventure = -1 no jogo livre.
    // Devolve os emblemas conquistados agora.
    std::vector<const Emblem *> record(const Result &r, int adventure);
    std::vector<const Emblem *> recordPhotos(int n);

    bool save(const std::string &path) const;
    bool load(const std::string &path);

private:
    std::vector<const Emblem *> checkEmblems(const Result *r);
};

} // namespace k3d::game
