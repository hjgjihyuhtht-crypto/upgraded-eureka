#include "game/game_common.h"

#include "game/bubbles.h"
#include "game/dodge.h"
#include "game/leaks.h"
#include "game/rebound.h"
#include "game/river.h"

namespace k3d::game {

void MiniGame::start(const GameConfig &cfg) {
    cfg_ = cfg;
    rng_ = Rng(cfg.seed * 2654435761u + static_cast<uint32_t>(id()) * 97u + cfg.difficulty * 7919u);
    score_ = 0;
    stats_.clear();
    playersSeen_ = 0;
    phase_ = Phase::Intro;
    introLeft_ = 3.f;
    outroLeft_ = 0;
    playTime_ = 0;
    lastCount_ = 4;
    onStart();
}

void MiniGame::update(float dt, const BodyFrame &body, Events &ev) {
    playersSeen_ = std::max(playersSeen_, body.count());
    switch (phase_) {
        case Phase::Intro: {
            introLeft_ -= dt;
            const int c = static_cast<int>(std::ceil(introLeft_));
            if (c != lastCount_ && c > 0) { ev.sfx.push_back(Sfx::Countdown); lastCount_ = c; }
            if (introLeft_ <= 0) {
                phase_ = Phase::Playing;
                ev.sfx.push_back(Sfx::Go);
                ev.say("Já!", 0.8f);
            }
            break;
        }
        case Phase::Playing:
            playTime_ += dt;
            onUpdate(dt, body, ev);
            break;
        case Phase::Outro:
            outroLeft_ -= dt;
            onUpdate(dt, body, ev);  // o mundo continua animando
            if (outroLeft_ <= 0) phase_ = Phase::Done;
            break;
        case Phase::Done:
            break;
    }
}

void MiniGame::finish(Events &ev) {
    if (phase_ != Phase::Playing) return;
    phase_ = Phase::Outro;
    outroLeft_ = 2.5f;
    ev.sfx.push_back(Sfx::Finish);
    if (ev.toast.empty()) ev.say("Fim!", 2.0f);
}

Result MiniGame::result() const {
    Result r;
    r.game = id();
    r.difficulty = cfg_.difficulty;
    r.score = score_;
    r.maxScore = std::max(1, maxScore());
    r.medal = medalFor(score_, r.maxScore);
    r.seconds = playTime_;
    r.playersSeen = std::max(1, playersSeen_);
    r.stats = stats_;
    return r;
}

Medal medalFor(int score, int maxScore) {
    if (maxScore <= 0) return Medal::None;
    const float f = static_cast<float>(score) / maxScore;
    if (f >= 0.90f) return Medal::Platinum;
    if (f >= 0.70f) return Medal::Gold;
    if (f >= 0.50f) return Medal::Silver;
    if (f >= 0.30f) return Medal::Bronze;
    return Medal::None;
}

const char *medalName(Medal m) {
    switch (m) {
        case Medal::Bronze: return "Bronze";
        case Medal::Silver: return "Prata";
        case Medal::Gold: return "Ouro";
        case Medal::Platinum: return "Platina";
        default: return "Sem medalha";
    }
}

const char *gameName(GameId g) {
    switch (g) {
        case GameId::River: return "Corredeira";
        case GameId::Rebound: return "Rebote";
        case GameId::Dodge: return "Esquiva";
        case GameId::Leaks: return "Aquário";
        case GameId::Bubbles: return "Gravidade Zero";
    }
    return "?";
}

const char *gameHowTo(GameId g) {
    switch (g) {
        case GameId::River: return "Dê passos ou incline o corpo para guiar o bote. Pule para saltar e pegar cristais no alto. Desvie das pedras!";
        case GameId::Rebound: return "Rebata as bolas com qualquer parte do corpo para quebrar os blocos no fim do corredor.";
        case GameId::Dodge: return "Pule as barras baixas, abaixe nas altas, dê um passo para o lado nas paredes e pegue os cristais com as mãos.";
        case GameId::Leaks: return "Tape as rachaduras do vidro com as mãos, os pés ou a cabeça antes que a água suba.";
        case GameId::Bubbles: return "Bata os braços para subir, pare para descer e dê passos para os lados. Estoure as bolhas com o corpo!";
    }
    return "";
}

const char *difficultyName(int d) {
    switch (d) {
        case 0: return "Fácil";
        case 1: return "Médio";
        default: return "Difícil";
    }
}

std::unique_ptr<MiniGame> createGame(GameId g) {
    switch (g) {
        case GameId::River: return std::make_unique<RiverGame>();
        case GameId::Rebound: return std::make_unique<ReboundGame>();
        case GameId::Dodge: return std::make_unique<DodgeGame>();
        case GameId::Leaks: return std::make_unique<LeaksGame>();
        case GameId::Bubbles: return std::make_unique<BubblesGame>();
    }
    return nullptr;
}

} // namespace k3d::game
