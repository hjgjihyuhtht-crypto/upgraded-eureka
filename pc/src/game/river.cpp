#include "game/river.h"

#include <algorithm>
#include <cmath>

namespace k3d::game {

float RiverGame::centerX(float s) const {
    return a1_ * std::sin(s / l1_ + p1_) + a2_ * std::sin(s / l2_ + p2_);
}

float RiverGame::heading(float s) const {
    return a1_ / l1_ * std::cos(s / l1_ + p1_) + a2_ / l2_ * std::cos(s / l2_ + p2_);
}

void RiverGame::onStart() {
    const int d = cfg_.difficulty;
    length = d == 0 ? 430.f : d == 1 ? 560.f : 680.f;
    halfWidth = d == 0 ? 5.2f : d == 1 ? 4.7f : 4.2f;
    baseSpeed = d == 0 ? 9.f : d == 1 ? 10.5f : 12.f;
    parTime = length / baseSpeed;
    a1_ = rng_.range(8, 14); l1_ = rng_.range(28, 40); p1_ = rng_.range(0, 6.28f);
    a2_ = rng_.range(3, 6);  l2_ = rng_.range(11, 16); p2_ = rng_.range(0, 6.28f);
    boatS = 0; boatLat = 0; boatY = 0; boatVy = 0; latVel = 0; speed = baseSpeed;
    bumpTimer = 0; splashTimer = 0; steer = 0; onWater = true;
    finishedCourse_ = false;
    photoHigh_ = false;
    lastJump_[0] = lastJump_[1] = -9;
    stat("rockHits", 0);
    stat("rings", 0);
    stat("maxHeight", 0);
    generate();
}

void RiverGame::generate() {
    gems.clear(); rocks.clear(); ramps.clear(); rings.clear();
    const int d = cfg_.difficulty;
    const float lim = halfWidth - 1.2f;
    float s = 35;
    while (s < length - 40) {
        const float r = rng_.uni();
        const float rockChance = d == 0 ? 0.25f : d == 1 ? 0.35f : 0.45f;
        if (r < 0.30f) {
            // fileira de cristais (reta ou em curva lateral)
            const int n = rng_.irange(5, 8);
            const float lat0 = rng_.range(-lim, lim), lat1 = clampf(lat0 + rng_.range(-3, 3), -lim, lim);
            for (int i = 0; i < n; ++i) gems.push_back({s + i * 3.2f, lat0 + (lat1 - lat0) * i / (n - 1), 0.7f});
            s += n * 3.2f + 10;
        } else if (r < 0.30f + rockChance) {
            // pedras com uma passagem, e cristais na passagem
            const float gap = rng_.range(-lim + 0.8f, lim - 0.8f);
            const int n = d == 0 ? 2 : 3;
            for (int i = 0; i < n; ++i) {
                float lat = rng_.range(-lim, lim);
                if (std::fabs(lat - gap) < 2.0f) lat = gap + (lat < gap ? -2.2f : 2.2f);
                if (std::fabs(lat) <= halfWidth - 0.5f) rocks.push_back({s + rng_.range(-2, 2), lat, rng_.range(0.6f, 1.0f)});
            }
            gems.push_back({s - 4, gap, 0.7f});
            gems.push_back({s, gap, 0.7f});
            gems.push_back({s + 4, gap, 0.7f});
            s += 22;
        } else if (r < 0.85f) {
            // rampa + arco de cristais no alto (+ anel nos níveis 1 e 2)
            const float lat = rng_.range(-lim * 0.7f, lim * 0.7f);
            ramps.push_back({s, lat, 1.6f, 5});
            for (int i = 0; i < 6; ++i) {
                const float t = i / 5.f;
                gems.push_back({s + 7 + i * 3.0f, lat, 1.2f + 2.6f * std::sin(t * 3.14159f)});
            }
            if (d >= 1 && rng_.chance(0.6f)) rings.push_back({s + 14.5f, lat, 3.8f});
            s += 36;
        } else {
            // zigue-zague
            const int n = 7;
            const float lat = rng_.range(-lim * 0.5f, lim * 0.5f);
            for (int i = 0; i < n; ++i) gems.push_back({s + i * 3.5f, clampf(lat + ((i & 1) ? 2.2f : -2.2f), -lim, lim), 0.7f});
            s += n * 3.5f + 10;
        }
    }
    // cristais "do pulo": alguns flutuam a 2,2 m sem rampa
    for (int i = 0; i < 4 + d * 2; ++i) {
        const float ss = rng_.range(60, length - 60);
        const float lat = rng_.range(-lim, lim);
        gems.push_back({ss, lat, 2.1f});
    }
    std::sort(gems.begin(), gems.end(), [](const Gem &a, const Gem &b) { return a.s < b.s; });
    gemTotal = static_cast<int>(gems.size());
}

void RiverGame::onUpdate(float dt, const BodyFrame &body, Events &ev) {
    const bool playing = phase() == Phase::Playing;

    // Direção: posição lateral + inclinação de cada jogador presente
    float sum = 0; int n = 0;
    bool jumped = false; int jumpers = 0;
    for (int i = 0; i < BodyFrame::kMaxPlayers; ++i) {
        const PlayerBody &p = body.players[i];
        if (!p.present) continue;
        sum += clampf(p.x / 0.45f, -1, 1) * 0.65f + clampf(p.lean / 0.30f, -1, 1) * 0.55f;
        ++n;
        if (p.jumpEvent) {
            jumped = true;
            lastJump_[i] = playTime();
        }
    }
    for (int i = 0; i < 2; ++i) if (playTime() - lastJump_[i] < 0.35) ++jumpers;
    steer = n ? clampf(sum / n, -1, 1) : 0;
    if (!playing) { steer = 0; jumped = false; }

    latVel = approach(latVel, steer * 7.0f, 14.f, dt);
    boatLat += latVel * dt;
    const float lim = halfWidth - 0.9f;
    if (boatLat > lim) { boatLat = lim; latVel = -std::fabs(latVel) * 0.4f; }
    if (boatLat < -lim) { boatLat = -lim; latVel = std::fabs(latVel) * 0.4f; }

    // Velocidade: recupera após batidas; para no fim
    const float target = finishedCourse_ ? 0.f : baseSpeed;
    speed = approach(speed, target, finishedCourse_ ? 6.f : 3.f, dt);
    boatS += speed * dt;

    // Rampa sob o bote?
    bool onRamp = false;
    for (const Ramp &r : ramps)
        if (boatS > r.s && boatS < r.s + r.length && std::fabs(boatLat - r.lat) < r.halfWidth + 0.4f) onRamp = true;

    // Pulo
    if (onWater && jumped) {
        boatVy = (jumpers >= 2 ? 7.0f : 5.6f) + (onRamp ? 3.4f : 0.f);
        onWater = false;
        ev.sfx.push_back(Sfx::Jump);
        statAdd("jumps");
    } else if (onWater && onRamp && boatVy <= 0) {
        // passar pela rampa sem pular dá um pulinho
        boatVy = 3.6f;
        onWater = false;
        ev.sfx.push_back(Sfx::Whoosh);
    }
    if (!onWater) {
        boatVy -= 14.f * dt;
        boatY += boatVy * dt;
        statMax("maxHeight", boatY);
        if (boatY > 2.6f && !photoHigh_ && playing) {
            photoHigh_ = true;
            ev.photos.push_back({"Corredeira: que salto!", -1});
        }
        if (boatY <= 0) {
            boatY = 0; boatVy = 0; onWater = true;
            ev.sfx.push_back(Sfx::Splash);
            splashTimer = 0.6f;
        }
    }
    bumpTimer = std::max(0.f, bumpTimer - dt);
    splashTimer = std::max(0.f, splashTimer - dt);
    if (!playing) return;

    // Cristais
    for (Gem &g : gems) {
        if (g.taken || g.s < boatS - 3) continue;
        if (g.s > boatS + 3) break;
        if (std::fabs(g.s - boatS) < 1.7f && std::fabs(g.lat - boatLat) < 1.35f &&
            boatY + 1.9f > g.y - 0.6f && boatY < g.y + 0.6f) {
            g.taken = true;
            addScore(1);
            ev.sfx.push_back(Sfx::Gem);
        }
    }
    // Anéis
    for (Ring &r : rings) {
        if (r.passed || std::fabs(r.s - boatS) > 1.0f) continue;
        if (std::fabs(r.lat - boatLat) < 1.3f && std::fabs(boatY + 0.6f - r.y) < 1.2f) {
            r.passed = true;
            addScore(5);
            statAdd("rings");
            ev.sfx.push_back(Sfx::Ring);
            ev.say("Anel! +5");
            ev.photos.push_back({"Corredeira: passou pelo anel!", -1});
        }
    }
    // Pedras
    for (Rock &r : rocks) {
        if (r.hit || std::fabs(r.s - boatS) > r.r + 1.2f) continue;
        if (std::fabs(r.lat - boatLat) < r.r + 0.9f && boatY < 0.7f) {
            r.hit = true;
            speed *= 0.45f;
            latVel = (boatLat > r.lat ? 1.f : -1.f) * 5.f;
            bumpTimer = 0.7f;
            addScore(-3);
            statAdd("rockHits");
            ev.sfx.push_back(Sfx::Bump);
            ev.shake = 0.8f;
            ev.say("Pedra! −3");
        }
    }

    if (!finishedCourse_ && boatS >= length) {
        finishedCourse_ = true;
        // bônus por tempo: até 10 pontos se chegar antes do tempo de referência
        const int bonus = static_cast<int>(clampf((parTime + 6.f - playTime()) * 1.0f, 0, 10));
        finish(ev);
        if (bonus > 0) { addScore(bonus); ev.say("Chegada! Bônus de tempo +" + std::to_string(bonus), 2.2f); }
        stat("timeBonus", static_cast<float>(bonus));
    }
}

} // namespace k3d::game
