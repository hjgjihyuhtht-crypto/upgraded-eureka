#pragma once
// Aquário: tapar rachaduras no vidro com o corpo antes que a água suba.
#include <vector>

#include "game/game_common.h"

namespace k3d::game {

class LeaksGame : public MiniGame {
public:
    enum class State { Cracking, Open, Sealed };
    struct Leak {
        V2 pos;
        float r = 0.13f;
        State state = State::Cracking;
        float t = 0;           // tempo no estado atual
        float cover = 0;       // segundos coberto (para selar)
        float sealedAt = -1;
    };
    struct Fish { float x, y, vx, phase; int color; };

    GameId id() const override { return GameId::Leaks; }
    float progress() const override { return totalLeaks ? clampf(static_cast<float>(spawned) / totalLeaks, 0, 1) : 0; }

    static constexpr float kTankHeight = 3.0f;
    std::vector<Leak> leaks;
    std::vector<Fish> fish;
    float water = 0;           // nível da água (m)
    int totalLeaks = 24, spawned = 0;
    float spawnTimer = 0;
    int openCount() const;

protected:
    void onStart() override;
    void onUpdate(float dt, const BodyFrame &body, Events &ev) override;
    int maxScore() const override { return totalLeaks; }

private:
    bool spawnLeak(const BodyFrame &body);
    std::vector<float> recentSeals_;
};

} // namespace k3d::game
