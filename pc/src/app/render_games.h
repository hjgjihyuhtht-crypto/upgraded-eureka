#pragma once
// Desenho 3D de cada minijogo. A lógica fica em src/game; aqui só a aparência.
#include <vector>

#include "app/render_common.h"
#include "body/body_types.h"

namespace k3d {

class App;
namespace game {
class RiverGame;
class ReboundGame;
class DodgeGame;
class LeaksGame;
class BubblesGame;
}

struct Particle { Vector3 p, v; float life, maxLife, size; Color c; float gravity; };

struct Particles {
    std::vector<Particle> list;
    void burst(Vector3 at, int n, Color c, float speed, float size, float life, float gravity = 6.f, unsigned seed = 1);
    void update(float dt);
    void draw() const;
};

// Memória visual entre quadros (efeitos que dependem de mudanças de estado)
struct RenderState {
    Particles fx;
    std::vector<char> prevFlags;
    int prevScore = 0;
    float clock = 0;
    unsigned seed = 1;
};

void renderRiver(App &app, const game::RiverGame &g, const RenderInfo &ri, RenderState &rs);
void renderRebound(App &app, const game::ReboundGame &g, const RenderInfo &ri, RenderState &rs);
void renderDodge(App &app, const game::DodgeGame &g, const RenderInfo &ri, RenderState &rs);
void renderLeaks(App &app, const game::LeaksGame &g, const RenderInfo &ri, RenderState &rs);
void renderBubbles(App &app, const game::BubblesGame &g, const RenderInfo &ri, RenderState &rs);

inline Vector3 v3(const V3 &v) { return Vector3{v.x, v.y, v.z}; }

// Mistura de cor simples
Color mixColor(Color a, Color b, float t);
// Quad colorido (sem iluminação) no mundo
void quad3D(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color ca, Color cb, Color cc, Color cd);
// Caixa com rotação em X (graus)
void drawBoxRotX(Vector3 p, Vector3 size, float degX, Color c);

} // namespace k3d
