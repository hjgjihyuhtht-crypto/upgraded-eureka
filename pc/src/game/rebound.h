#pragma once
// Rebote: rebater bolas com o corpo para quebrar os blocos no fim do corredor.
#include <vector>

#include "game/game_common.h"

namespace k3d::game {

class ReboundGame : public MiniGame {
public:
    static constexpr float kHalfW = 1.8f, kHeight = 2.8f, kTargetZ = -12.0f, kBallR = 0.13f;
    struct Ball { V3 pos, vel; int targetsThisTrip = 0; float age = 0; bool alive = true; float lastHit = -9; };
    struct Target { float x, y, w, h; int hp; bool broken = false; float flash = 0; };

    GameId id() const override { return GameId::Rebound; }
    float progress() const override;

    std::vector<Ball> balls;
    std::vector<Target> targets;
    int ballsTotal = 8, ballsSpawned = 0, ballsLost = 0, maxActive = 1;
    float spawnTimer = 0, launcherX = 0, launcherY = 1.2f;
    float timeLimit = 110;
    int brokenCount() const;

protected:
    void onStart() override;
    void onUpdate(float dt, const BodyFrame &body, Events &ev) override;
    int maxScore() const override { return static_cast<int>(targets.size()); }

private:
    void launch();
    bool cleared_ = false;
};

} // namespace k3d::game
