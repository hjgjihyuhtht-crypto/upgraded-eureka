#pragma once
// Esquiva: plataforma em movimento; pular, abaixar, desviar e pegar cristais.
#include <vector>

#include "game/game_common.h"

namespace k3d::game {

class DodgeGame : public MiniGame {
public:
    enum class Kind { LowBar, HighBar, WallLeftGap, WallRightGap, Gem };
    struct Obstacle {
        Kind kind;
        float z;            // posição ao longo do percurso (negativo = à frente)
        float x = 0, y = 0; // cristal: posição; parede: centro da abertura
        float param = 0;    // parede: largura da abertura
        bool resolved = false, hit = false, taken = false;
    };

    GameId id() const override { return GameId::Dodge; }
    float progress() const override { return clampf(traveled / courseLength, 0, 1); }

    std::vector<Obstacle> items;
    float speed = 6, traveled = 0, courseLength = 400;
    float invuln[2] = {0, 0};
    float stumble = 0;      // animação de tropeço
    int gemTotal = 0;
    float refHeight = 1.7f; // altura usada para desenhar barras altas
    float reachTop = 1.95f; // altura máxima alcançável e visível (atualizada pelo corpo)
    float gemY(const Obstacle &o) const { return std::min(o.y, reachTop); }
    // Altura relativa das barras (fração da altura em pé do jogador)
    static constexpr float kLowBarTop = 0.22f;   // metros acima do chão
    static constexpr float kHighBarBottom = 0.84f; // × altura em pé

    // Próximo obstáculo (para mostrar a dica na tela)
    const Obstacle *nextHazard() const;

protected:
    void onStart() override;
    void onUpdate(float dt, const BodyFrame &body, Events &ev) override;
    int maxScore() const override { return gemTotal; }

private:
    void generate();
    bool photoJump_ = false, photoDuck_ = false;
};

const char *dodgeHint(DodgeGame::Kind k);

} // namespace k3d::game
