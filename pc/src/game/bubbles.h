#pragma once
// Gravidade Zero: flutuar batendo os braços e estourar bolhas com o corpo.
#include <vector>

#include "game/game_common.h"

namespace k3d::game {

class BubblesGame : public MiniGame {
public:
    struct Bubble { V2 pos, vel; float r; int value; float life; bool popped = false; float popT = 0; };
    struct Floater { float y = 0, vy = 0, x = 0; float flapGlow = 0; };

    GameId id() const override { return GameId::Bubbles; }
    float progress() const override { return clampf(playTime() / duration, 0, 1); }

    static constexpr float kRoomHalfW = 2.8f, kRoomH = 7.5f;
    std::vector<Bubble> bubbles;
    Floater floaters[2];
    float duration = 70;
    int totalValue = 0;       // soma dos valores de todas as bolhas da partida
    // Deslocamento da silhueta do jogador i no mundo do jogo (x extra, altura)
    V2 offset(int i, const PlayerBody &p) const { return {floaters[i].x - p.x, floaters[i].y}; }

protected:
    void onStart() override;
    void onUpdate(float dt, const BodyFrame &body, Events &ev) override;
    int maxScore() const override { return totalValue; }

private:
    struct Planned { float t; Bubble b; };
    std::vector<Planned> plan_;
    size_t next_ = 0;
    std::vector<float> pops_;
};

} // namespace k3d::game
