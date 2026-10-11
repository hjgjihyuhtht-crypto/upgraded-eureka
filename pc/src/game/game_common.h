#pragma once
// Base dos minijogos de "Expedição Ativa" — jogos ORIGINAIS de movimento corporal.
// Lógica pura (sem gráficos), determinística por semente, testável sem tela.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "body/body_types.h"

namespace k3d::game {

enum class GameId : int { River = 0, Rebound = 1, Dodge = 2, Leaks = 3, Bubbles = 4 };
constexpr int kGameCount = 5;

enum class Medal : int { None = 0, Bronze = 1, Silver = 2, Gold = 3, Platinum = 4 };

enum class Sfx : int {
    Gem, Bump, Splash, Jump, Hit, Break, Whoosh, Pop, Crack, Plug, Countdown, Go,
    Finish, MedalSfx, Miss, Click, Hover, Ring, Flap, Count
};

struct PhotoRequest { std::string caption; int player = -1; };

struct Events {
    std::vector<Sfx> sfx;
    std::vector<PhotoRequest> photos;
    std::string toast;         // mensagem curta de destaque
    float toastSeconds = 0;
    float shake = 0;           // 0..1, tremor da câmera
    void clear() { sfx.clear(); photos.clear(); toast.clear(); toastSeconds = 0; shake = 0; }
    void say(const std::string &t, float secs = 1.6f) { toast = t; toastSeconds = secs; }
};

struct GameConfig {
    int difficulty = 0;   // 0 fácil, 1 médio, 2 difícil
    uint32_t seed = 1;
    int players = 1;      // 1 ou 2 (informativo; os jogos usam quem estiver presente)
};

struct Result {
    GameId game = GameId::River;
    int difficulty = 0;
    int score = 0;
    int maxScore = 1;
    Medal medal = Medal::None;
    float seconds = 0;
    int playersSeen = 1;
    std::map<std::string, float> stats;
    float stat(const std::string &k) const { auto it = stats.find(k); return it == stats.end() ? 0.f : it->second; }
};

enum class Phase { Intro, Playing, Outro, Done };

struct Rng {
    uint32_t s = 1;
    explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (next() & 0xFFFFFF) / 16777216.f; }
    float range(float a, float b) { return a + (b - a) * uni(); }
    int irange(int a, int b) { return a + static_cast<int>(next() % static_cast<uint32_t>(b - a + 1)); }
    bool chance(float p) { return uni() < p; }
};

class MiniGame {
public:
    virtual ~MiniGame() = default;
    virtual GameId id() const = 0;

    void start(const GameConfig &cfg);
    // dt em segundos; corpo atual; eventos produzidos neste passo.
    void update(float dt, const BodyFrame &body, Events &ev);

    Phase phase() const { return phase_; }
    bool done() const { return phase_ == Phase::Done; }
    float introLeft() const { return introLeft_; }
    float playTime() const { return playTime_; }
    int score() const { return score_; }
    const GameConfig &config() const { return cfg_; }
    Result result() const;
    // Fração de progresso da partida (0..1), para a barra no topo.
    virtual float progress() const = 0;

protected:
    virtual void onStart() = 0;
    virtual void onUpdate(float dt, const BodyFrame &body, Events &ev) = 0;
    virtual int maxScore() const = 0;
    void finish(Events &ev);   // termina a partida (vai para Outro)
    void addScore(int n) { score_ = std::max(0, score_ + n); }
    void stat(const std::string &k, float v) { stats_[k] = v; }
    void statAdd(const std::string &k, float v = 1) { stats_[k] += v; }
    void statMax(const std::string &k, float v) { if (v > stats_[k]) stats_[k] = v; }

    GameConfig cfg_;
    Rng rng_;
    int score_ = 0;
    std::map<std::string, float> stats_;
    int playersSeen_ = 0;

private:
    Phase phase_ = Phase::Intro;
    float introLeft_ = 3.f;
    float outroLeft_ = 0;
    float playTime_ = 0;
    int lastCount_ = 4;
};

Medal medalFor(int score, int maxScore);
const char *medalName(Medal m);
const char *gameName(GameId g);
const char *gameHowTo(GameId g);
const char *difficultyName(int d);
std::unique_ptr<MiniGame> createGame(GameId g);

// Ajudas
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float approach(float v, float target, float rate, float dt) {
    const float d = target - v, m = rate * dt;
    return std::fabs(d) <= m ? target : v + (d > 0 ? m : -m);
}

} // namespace k3d::game
