#include "game/dodge.h"

#include <algorithm>
#include <cmath>

namespace k3d::game {

const char *dodgeHint(DodgeGame::Kind k) {
    switch (k) {
        case DodgeGame::Kind::LowBar: return "PULE!";
        case DodgeGame::Kind::HighBar: return "ABAIXE!";
        case DodgeGame::Kind::WallLeftGap: return "PASSO À ESQUERDA!";
        case DodgeGame::Kind::WallRightGap: return "PASSO À DIREITA!";
        default: return "";
    }
}

const DodgeGame::Obstacle *DodgeGame::nextHazard() const {
    const Obstacle *best = nullptr;
    for (const auto &o : items) {
        if (o.kind == Kind::Gem || o.resolved) continue;
        const float dz = traveled - o.z;  // distância até o jogador (positiva = já passou)
        if (dz > 0) continue;
        if (!best || o.z > best->z) best = &o;
    }
    return best;
}

void DodgeGame::onStart() {
    const int d = cfg_.difficulty;
    speed = d == 0 ? 6.f : d == 1 ? 7.2f : 8.4f;
    courseLength = speed * (d == 0 ? 62.f : d == 1 ? 70.f : 78.f);
    traveled = 0;
    invuln[0] = invuln[1] = 0;
    stumble = 0;
    photoJump_ = photoDuck_ = false;
    stat("collisions", 0);
    generate();
}

void DodgeGame::generate() {
    items.clear();
    const int d = cfg_.difficulty;
    float z = -25;   // primeiro evento 25 m à frente
    const float gapTime = d == 0 ? 2.6f : d == 1 ? 2.1f : 1.7f;
    Kind last = Kind::Gem;
    while (-z < courseLength - 15) {
        const float r = rng_.uni();
        Obstacle o;
        o.z = z;
        if (r < 0.24f) o.kind = Kind::LowBar;
        else if (r < 0.48f) o.kind = Kind::HighBar;
        else if (r < 0.62f) o.kind = Kind::WallLeftGap;
        else if (r < 0.76f) o.kind = Kind::WallRightGap;
        else o.kind = Kind::Gem;
        if (o.kind == last && o.kind != Kind::Gem && rng_.chance(0.5f)) o.kind = Kind::Gem;
        if (o.kind == Kind::WallLeftGap) { o.x = -0.85f; o.param = d == 2 ? 0.9f : 1.05f; }
        if (o.kind == Kind::WallRightGap) { o.x = 0.85f; o.param = d == 2 ? 0.9f : 1.05f; }
        if (o.kind == Kind::Gem) {
            // grupo de 3 cristais em posições de alcance: alto, lados, baixo
            const int pattern = rng_.irange(0, 3);
            for (int i = 0; i < 3; ++i) {
                Obstacle g;
                g.kind = Kind::Gem;
                g.z = z - i * 1.2f * speed / 6.f;
                switch (pattern) {
                    case 0: g.x = -0.6f + i * 0.6f; g.y = 1.95f; break;                  // em cima (braços para o alto)
                    case 1: g.x = (i == 1 ? 0.f : (i == 0 ? -0.75f : 0.75f)); g.y = 1.25f; break; // lados
                    case 2: g.x = rng_.range(-0.7f, 0.7f); g.y = rng_.range(0.9f, 1.9f); break;
                    default: g.x = -0.75f + i * 0.75f; g.y = 0.75f + i * 0.45f; break;   // diagonal
                }
                items.push_back(g);
            }
        } else {
            items.push_back(o);
            // cristais de recompensa logo depois de alguns obstáculos
            if (rng_.chance(0.45f)) {
                Obstacle g;
                g.kind = Kind::Gem;
                g.z = z - 0.8f * speed;
                g.x = o.kind == Kind::WallLeftGap ? -0.85f : o.kind == Kind::WallRightGap ? 0.85f : rng_.range(-0.6f, 0.6f);
                g.y = o.kind == Kind::LowBar ? 1.9f : 1.2f;
                items.push_back(g);
            }
        }
        last = o.kind;
        z -= speed * gapTime * rng_.range(0.85f, 1.15f);
    }
    gemTotal = 0;
    for (auto &o : items) gemTotal += o.kind == Kind::Gem;
}

void DodgeGame::onUpdate(float dt, const BodyFrame &body, Events &ev) {
    const bool playing = phase() == Phase::Playing;
    stumble = std::max(0.f, stumble - dt);
    for (float &v : invuln) v = std::max(0.f, v - dt);
    if (!playing) { traveled += speed * dt * 0.3f; return; }

    if (const PlayerBody *p = body.first()) {
        refHeight = p->standY > 1.0f ? p->standY : refHeight;
        reachTop = std::max(1.2f, p->reachTop());
    }
    const float prevT = traveled;
    traveled += speed * dt;

    for (auto &o : items) {
        if (o.resolved) continue;
        // o objeto cruza o plano do jogador quando traveled passa por -o.z
        const float at = -o.z;
        if (o.kind == Kind::Gem) {
            if (traveled < at - 0.35f) continue;
            if (traveled > at + 0.35f) { o.resolved = true; continue; }
            for (int i = 0; i < BodyFrame::kMaxPlayers; ++i) {
                const PlayerBody &p = body.players[i];
                if (!p.present) continue;
                if (p.grid.overlapCircle({o.x, gemY(o)}, 0.17f).cells > 0) {
                    o.taken = true;
                    o.resolved = true;
                    addScore(1);
                    ev.sfx.push_back(Sfx::Gem);
                    break;
                }
            }
            continue;
        }
        if (!(prevT < at && traveled >= at)) continue;
        o.resolved = true;
        for (int i = 0; i < BodyFrame::kMaxPlayers; ++i) {
            const PlayerBody &p = body.players[i];
            if (!p.present) continue;
            const float H = p.standY > 1.0f ? p.standY : 1.7f;
            int cells = 0;
            bool hit = false;
            switch (o.kind) {
                case Kind::LowBar:
                    cells = p.grid.overlapRect(-2.f, 0.f, 2.f, kLowBarTop);
                    hit = cells >= 4 && !p.airborne;
                    if (!hit && p.airborne && p.jumpHeight > 0.3f && !photoJump_) {
                        photoJump_ = true;
                        ev.photos.push_back({"Esquiva: pulo perfeito!", i});
                    }
                    break;
                case Kind::HighBar:
                    cells = p.grid.overlapRect(-2.f, kHighBarBottom * H, 2.f, 2.6f);
                    hit = cells >= 4;
                    if (!hit && p.ducking && !photoDuck_) {
                        photoDuck_ = true;
                        ev.photos.push_back({"Esquiva: por baixo da barra!", i});
                    }
                    break;
                case Kind::WallLeftGap:
                case Kind::WallRightGap: {
                    const float g0 = o.x - o.param * 0.5f, g1 = o.x + o.param * 0.5f;
                    cells = p.grid.overlapRect(-2.f, 0.f, g0, 2.6f) + p.grid.overlapRect(g1, 0.f, 2.f, 2.6f);
                    hit = cells >= 6;
                    break;
                }
                default: break;
            }
            if (hit && invuln[i] <= 0) {
                o.hit = true;
                invuln[i] = 0.9f;
                stumble = 0.6f;
                addScore(-2);
                statAdd("collisions");
                ev.sfx.push_back(Sfx::Bump);
                ev.shake = 0.7f;
                ev.say("Ai! −2");
            }
        }
        if (!o.hit) ev.sfx.push_back(Sfx::Whoosh);
        if (o.kind == Kind::LowBar) statAdd("jumpsNeeded");
    }
    for (int i = 0; i < BodyFrame::kMaxPlayers; ++i)
        if (body.players[i].present && body.players[i].jumpEvent) statAdd("jumps");

    if (traveled >= courseLength) finish(ev);
}

} // namespace k3d::game
