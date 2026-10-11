#pragma once
// "Jogador robô": move bonecos sintéticos para jogar cada minijogo.
// Usado nos testes automáticos e no modo Demonstração (identificado como simulação).
#include "body/sim_depth.h"
#include "game/game_common.h"

namespace k3d::game {

class Autopilot {
public:
    explicit Autopilot(float skill = 1.0f) : skill_(skill) {}
    // Atualiza as poses a partir do estado do jogo e do corpo rastreado (o que o jogo "vê").
    void update(float dt, const MiniGame &g, const BodyFrame &body, int players);
    // Menus: aponta a mão direita para (x,y) no plano de jogo.
    void pointAt(V2 target, float dt);
    SimPose pose[2];

private:
    void jump(int i) { if (jumpT_[i] <= 0) jumpT_[i] = 0.55f; }
    void flap(int i) { if (flapT_[i] <= 0) flapT_[i] = 0.55f; }
    void moveX(int i, float target, float dt);
    void animate(int i, float dt);
    float skill_;
    float jumpT_[2] = {0, 0};
    float flapT_[2] = {0, 0};
    float t_ = 0;
};

} // namespace k3d::game
