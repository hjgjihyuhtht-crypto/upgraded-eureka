#include "game/bubbles.h"

#include <algorithm>
#include <cmath>

namespace k3d::game {

void BubblesGame::onStart() {
    const int d = cfg_.difficulty;
    duration = d == 0 ? 65.f : d == 1 ? 72.f : 80.f;
    bubbles.clear();
    plan_.clear();
    next_ = 0;
    pops_.clear();
    for (auto &f : floaters) f = Floater{};
    stat("maxFloat", 0);
    stat("maxCombo", 0);

    // Plano de bolhas: ondas em posições alcançáveis, com alturas crescentes
    const int n = d == 0 ? 45 : d == 1 ? 58 : 72;
    totalValue = 0;
    for (int i = 0; i < n; ++i) {
        Planned p;
        p.t = 1.0f + (duration - 8.f) * i / n + rng_.range(-0.3f, 0.3f);
        const float heightMax = 1.6f + (kRoomH - 2.4f) * clampf(p.t / (duration * 0.6f), 0.2f, 1.f);
        Bubble b;
        b.pos = V2(rng_.range(-kRoomHalfW + 0.4f, kRoomHalfW - 0.4f), rng_.range(0.5f, heightMax));
        b.vel = V2(rng_.range(-0.25f, 0.25f), rng_.range(-0.1f, 0.2f));
        b.r = rng_.range(0.17f, 0.32f);
        b.value = rng_.chance(0.1f) ? 3 : 1;   // bolha dourada vale 3
        b.life = d == 0 ? 9.f : d == 1 ? 7.5f : 6.5f;
        p.b = b;
        totalValue += b.value;
        plan_.push_back(p);
    }
    std::sort(plan_.begin(), plan_.end(), [](const Planned &a, const Planned &b) { return a.t < b.t; });
}

void BubblesGame::onUpdate(float dt, const BodyFrame &body, Events &ev) {
    const bool playing = phase() == Phase::Playing;

    // Flutuação de cada jogador
    for (int i = 0; i < BodyFrame::kMaxPlayers; ++i) {
        const PlayerBody &p = body.players[i];
        Floater &f = floaters[i];
        f.flapGlow = std::max(0.f, f.flapGlow - dt * 2);
        if (!p.present) { f.vy -= 1.6f * dt; f.y = std::max(0.f, f.y + f.vy * dt); if (f.y == 0) f.vy = 0; continue; }
        // posição lateral: o passo físico é ampliado para cobrir a sala
        f.x = approach(f.x, clampf(p.x * 1.9f + p.lean * 1.2f, -kRoomHalfW + 0.3f, kRoomHalfW - 0.3f), 4.5f, dt);
        if (playing && p.flapEvent) {
            f.vy = std::min(3.6f, std::max(f.vy, 0.f) + 1.4f + 1.6f * p.flapStrength);
            f.flapGlow = 1;
            ev.sfx.push_back(Sfx::Flap);
        }
        if (playing && p.jumpEvent) { f.vy = std::min(3.6f, std::max(f.vy, 0.f) + 2.4f); f.flapGlow = 1; }
        f.vy -= 1.6f * dt;               // gravidade baixa
        f.vy *= std::pow(0.7f, dt);      // arrasto
        f.y += f.vy * dt;
        const float top = kRoomH - std::max(1.7f, p.standY) - 0.1f;
        if (f.y > top) { f.y = top; f.vy = std::min(0.f, f.vy); }
        if (f.y < 0) { f.y = 0; f.vy = 0; }
        statMax("maxFloat", f.y);
        if (f.y > 4.0f && stats_["photoHigh"] == 0 && playing) {
            stat("photoHigh", 1);
            ev.photos.push_back({"Gravidade Zero: lá no alto!", i});
        }
    }

    // Bolhas
    for (auto &b : bubbles) {
        if (b.popped) { b.popT += dt; continue; }
        b.pos = b.pos + b.vel * dt;
        b.pos.y += std::sin(b.life * 1.7f) * 0.08f * dt;
        if (b.pos.x < -kRoomHalfW + b.r || b.pos.x > kRoomHalfW - b.r) b.vel.x = -b.vel.x;
        b.pos.y = clampf(b.pos.y, b.r, kRoomH - b.r);
        b.life -= dt;
    }
    bubbles.erase(std::remove_if(bubbles.begin(), bubbles.end(),
                                 [](const Bubble &b) { return (b.popped && b.popT > 0.4f) || (!b.popped && b.life <= 0); }),
                  bubbles.end());
    if (!playing) return;

    while (next_ < plan_.size() && plan_[next_].t <= playTime()) {
        bubbles.push_back(plan_[next_].b);
        ++next_;
    }
    for (auto &b : bubbles) {
        if (b.popped) continue;
        for (int i = 0; i < BodyFrame::kMaxPlayers; ++i) {
            const PlayerBody &p = body.players[i];
            if (!p.present) continue;
            if (p.grid.overlapCircle(b.pos, b.r, offset(i, p)).cells > 0) {
                b.popped = true;
                addScore(b.value);
                ev.sfx.push_back(b.value > 1 ? Sfx::Ring : Sfx::Pop);
                pops_.push_back(playTime());
                break;
            }
        }
    }
    pops_.erase(std::remove_if(pops_.begin(), pops_.end(), [&](float t) { return playTime() - t > 2.0f; }), pops_.end());
    if (static_cast<float>(pops_.size()) > stats_["maxCombo"]) {
        stat("maxCombo", static_cast<float>(pops_.size()));
        if (pops_.size() == 5) {
            ev.say("Estoura-tudo!");
            ev.photos.push_back({"Gravidade Zero: cinco bolhas seguidas!", -1});
        }
    }
    if (playTime() >= duration) finish(ev);
}

} // namespace k3d::game
