#pragma once
// Corredeira: guiar um bote rio abaixo com o corpo.
#include <vector>

#include "game/game_common.h"

namespace k3d::game {

class RiverGame : public MiniGame {
public:
    struct Gem { float s, lat, y; bool taken = false; };
    struct Rock { float s, lat, r; bool hit = false; };
    struct Ramp { float s, lat, halfWidth, length; };
    struct Ring { float s, lat, y; bool passed = false; };

    GameId id() const override { return GameId::River; }
    float progress() const override { return clampf(boatS / length, 0, 1); }

    // Rio
    float length = 500, halfWidth = 5, baseSpeed = 10;
    float centerX(float s) const;     // eixo do rio no mundo (para desenhar)
    float heading(float s) const;     // derivada do eixo
    // Bote
    float boatS = 0, boatLat = 0, boatY = 0, boatVy = 0, latVel = 0, speed = 0;
    float bumpTimer = 0, splashTimer = 0, steer = 0;
    bool onWater = true;
    float parTime = 50;
    std::vector<Gem> gems;
    std::vector<Rock> rocks;
    std::vector<Ramp> ramps;
    std::vector<Ring> rings;
    int gemTotal = 0;

protected:
    void onStart() override;
    void onUpdate(float dt, const BodyFrame &body, Events &ev) override;
    int maxScore() const override { return gemTotal + static_cast<int>(rings.size()) * 5; }

private:
    void generate();
    float a1_ = 0, a2_ = 0, l1_ = 1, l2_ = 1, p1_ = 0, p2_ = 0;
    bool finishedCourse_ = false;
    double lastJump_[2] = {-9, -9};
    bool photoHigh_ = false;
};

} // namespace k3d::game
