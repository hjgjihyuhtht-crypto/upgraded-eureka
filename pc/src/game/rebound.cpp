#include "game/rebound.h"

#include <algorithm>
#include <cmath>

namespace k3d::game {

int ReboundGame::brokenCount() const {
    int n = 0;
    for (auto &t : targets) n += t.broken;
    return n;
}

float ReboundGame::progress() const {
    const float a = ballsTotal ? static_cast<float>(ballsLost) / ballsTotal : 0.f;
    const float b = targets.empty() ? 0.f : static_cast<float>(brokenCount()) / targets.size();
    return clampf(std::max(a, b), 0, 1);
}

void ReboundGame::onStart() {
    const int d = cfg_.difficulty;
    balls.clear();
    targets.clear();
    const int cols = 6, rows = d == 0 ? 3 : d == 1 ? 4 : 5;
    const float w = (2 * kHalfW - 0.3f) / cols;
    const float h = (d == 0 ? 0.55f : d == 1 ? 0.45f : 0.40f);
    const float y0 = 0.35f;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            Target t;
            t.x = -kHalfW + 0.15f + (c + 0.5f) * w;
            t.y = y0 + (r + 0.5f) * h;
            t.w = w * 0.92f;
            t.h = h * 0.9f;
            // blocos reforçados (2 batidas) no difícil
            t.hp = (d == 2 && rng_.chance(0.25f)) ? 2 : 1;
            targets.push_back(t);
        }
    ballsTotal = d == 0 ? 8 : d == 1 ? 11 : 14;
    ballsSpawned = 0;
    ballsLost = 0;
    maxActive = 1;
    spawnTimer = 1.0f;
    timeLimit = d == 0 ? 100.f : d == 1 ? 115.f : 130.f;
    cleared_ = false;
    stat("maxTargetsOneBall", 0);
    stat("hits", 0);
}

void ReboundGame::launch() {
    const int d = cfg_.difficulty;
    Ball b;
    launcherX = rng_.range(-1.2f, 1.2f);
    launcherY = rng_.range(0.8f, 2.0f);
    b.pos = V3(launcherX, launcherY, kTargetZ + 1.5f);
    const float tx = rng_.range(-0.75f, 0.75f), ty = rng_.range(0.5f, 1.8f);
    const float speed = d == 0 ? 7.f : d == 1 ? 8.5f : 10.f;
    const float dz = -b.pos.z;
    const float tFlight = dz / speed;
    const float g = 4.f;
    b.vel = V3((tx - b.pos.x) / tFlight, (ty - b.pos.y) / tFlight + 0.5f * g * tFlight, speed);
    balls.push_back(b);
    ++ballsSpawned;
}

void ReboundGame::onUpdate(float dt, const BodyFrame &body, Events &ev) {
    const bool playing = phase() == Phase::Playing;
    for (auto &t : targets) t.flash = std::max(0.f, t.flash - dt);
    if (!playing) {
        for (auto &b : balls) { b.pos = b.pos + b.vel * dt; }
        return;
    }

    // Mais bolas ao mesmo tempo com o passar do jogo
    const int d = cfg_.difficulty;
    const float frac = static_cast<float>(ballsSpawned) / ballsTotal;
    maxActive = 1 + (frac > 0.35f ? 1 : 0) + (d >= 1 && frac > 0.6f ? 1 : 0) + (d == 2 && frac > 0.8f ? 1 : 0);
    int alive = 0;
    for (auto &b : balls) alive += b.alive;
    spawnTimer -= dt;
    if (alive < maxActive && ballsSpawned < ballsTotal && spawnTimer <= 0) {
        launch();
        spawnTimer = 1.4f;
        ev.sfx.push_back(Sfx::Whoosh);
    }

    const float g = 4.f;
    for (auto &b : balls) {
        if (!b.alive) continue;
        b.age += dt;
        const V3 prev = b.pos;
        b.vel.y -= g * dt;
        b.pos = b.pos + b.vel * dt;
        // paredes, chão e teto
        if (b.pos.x < -kHalfW + kBallR) { b.pos.x = -kHalfW + kBallR; b.vel.x = std::fabs(b.vel.x) * 0.9f; }
        if (b.pos.x > kHalfW - kBallR) { b.pos.x = kHalfW - kBallR; b.vel.x = -std::fabs(b.vel.x) * 0.9f; }
        if (b.pos.y < kBallR) { b.pos.y = kBallR; b.vel.y = std::fabs(b.vel.y) * 0.85f + 1.0f; }
        if (b.pos.y > kHeight - kBallR) { b.pos.y = kHeight - kBallR; b.vel.y = -std::fabs(b.vel.y) * 0.9f; }

        // Cruzou o plano do jogador vindo de frente?
        if (prev.z < 0 && b.pos.z >= 0 && b.vel.z > 0) {
            const float t = -prev.z / (b.pos.z - prev.z);
            const V2 at(prev.x + (b.pos.x - prev.x) * t, prev.y + (b.pos.y - prev.y) * t);
            for (int i = 0; i < BodyFrame::kMaxPlayers; ++i) {
                const PlayerBody &p = body.players[i];
                if (!p.present) continue;
                const auto hit = p.grid.overlapCircle(at, kBallR + 0.07f);
                if (hit.cells < 1) continue;
                // velocidade da mão mais próxima dá "força" ao rebote
                V2 hv;
                const float dl = (p.leftHand - at).len(), dr = (p.rightHand - at).len();
                if (std::min(dl, dr) < 0.3f) hv = dl < dr ? p.leftHandVel : p.rightHandVel;
                const float sp = std::min(15.f, std::fabs(b.vel.z) * 1.04f + 0.5f);
                b.vel.z = -sp;
                b.vel.x = clampf(b.vel.x * 0.3f + hit.normal.x * 2.5f + hv.x * 0.45f, -5, 5);
                b.vel.y = clampf(hit.normal.y * 2.5f + hv.y * 0.35f + 2.2f, -3, 6);
                b.pos.z = -0.01f;
                b.targetsThisTrip = 0;
                b.lastHit = playTime();
                statAdd("hits");
                ev.sfx.push_back(Sfx::Hit);
                break;
            }
        }
        // Passou do jogador: perdida
        if (b.pos.z > 1.2f) {
            b.alive = false;
            ++ballsLost;
            ev.sfx.push_back(Sfx::Miss);
            continue;
        }
        // Chegou aos blocos
        if (b.pos.z <= kTargetZ + kBallR && b.vel.z < 0) {
            b.pos.z = kTargetZ + kBallR;
            for (auto &tg : targets) {
                if (tg.broken) continue;
                if (std::fabs(b.pos.x - tg.x) < tg.w * 0.5f + kBallR && std::fabs(b.pos.y - tg.y) < tg.h * 0.5f + kBallR) {
                    tg.flash = 0.3f;
                    if (--tg.hp <= 0) {
                        tg.broken = true;
                        addScore(1);
                        ++b.targetsThisTrip;
                        statMax("maxTargetsOneBall", static_cast<float>(b.targetsThisTrip));
                        ev.sfx.push_back(Sfx::Break);
                        if (b.targetsThisTrip == 3) {
                            ev.say("Três com uma bola!");
                            ev.photos.push_back({"Rebote: três blocos de uma vez!", -1});
                        }
                    } else {
                        ev.sfx.push_back(Sfx::Bump);
                    }
                    break;
                }
            }
            // volta para o jogador, mirando a área de jogo
            b.vel.z = std::fabs(b.vel.z) * 0.95f;
            b.vel.x = clampf(b.vel.x * 0.5f + (rng_.range(-0.6f, 0.6f) - b.pos.x) * 0.25f, -3, 3);
            b.vel.y = clampf(b.vel.y * 0.5f + 1.5f, -1, 4);
            // após bater nos blocos, a bola pode quebrar outros no caminho (efeito de ricochete)
        }
    }
    balls.erase(std::remove_if(balls.begin(), balls.end(), [](const Ball &b) { return !b.alive; }), balls.end());

    if (!cleared_ && brokenCount() == static_cast<int>(targets.size())) {
        cleared_ = true;
        stat("cleared", 1);
        ev.say("Paredão limpo!", 2.2f);
        ev.photos.push_back({"Rebote: paredão limpo!", -1});
        finish(ev);
        return;
    }
    if ((ballsSpawned >= ballsTotal && balls.empty()) || playTime() >= timeLimit) finish(ev);
}

} // namespace k3d::game
