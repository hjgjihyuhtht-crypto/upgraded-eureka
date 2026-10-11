#include "game/leaks.h"

#include <algorithm>
#include <cmath>

namespace k3d::game {

int LeaksGame::openCount() const {
    int n = 0;
    for (auto &l : leaks) n += l.state != State::Sealed;
    return n;
}

void LeaksGame::onStart() {
    const int d = cfg_.difficulty;
    leaks.clear();
    fish.clear();
    water = 0;
    totalLeaks = d == 0 ? 22 : d == 1 ? 30 : 38;
    spawned = 0;
    spawnTimer = 0.5f;
    recentSeals_.clear();
    for (int i = 0; i < 9; ++i)
        fish.push_back({rng_.range(-3, 3), rng_.range(0.3f, 2.6f), rng_.range(0.4f, 1.1f) * (rng_.chance(0.5f) ? 1 : -1),
                        rng_.range(0, 6.28f), rng_.irange(0, 4)});
    stat("maxWaterFrac", 0);
    stat("maxSimultaneousSeals", 0);
}

bool LeaksGame::spawnLeak(const BodyFrame &body) {
    // Perto de um jogador (ao alcance), mas fora do corpo dele agora.
    const PlayerBody *ps[2];
    int np = 0;
    for (auto &p : body.players) if (p.present) ps[np++] = &p;
    const int d = cfg_.difficulty;
    for (int attempt = 0; attempt < 30; ++attempt) {
        V2 pos;
        float H = 1.7f, cx = 0, top = 1.95f, minX = -1.3f, maxX = 1.3f;
        if (np) {
            const PlayerBody *p = ps[rng_.irange(0, np - 1)];
            H = p->standY > 1.0f ? p->standY : 1.7f;
            cx = p->x * 0.6f;   // puxa para o centro da área de jogo
            top = p->reachTop();
            minX = p->visibleMinX + 0.2f;
            maxX = p->visibleMaxX - 0.2f;
        }
        const float kind = rng_.uni();
        if (kind < 0.18f) pos = V2(cx + rng_.range(-0.55f, 0.55f), rng_.range(0.08f, 0.25f));              // pés
        else if (kind < (d == 0 ? 0.25f : 0.35f)) pos = V2(cx + rng_.range(-0.6f, 0.6f), H + rng_.range(0.05f, 0.30f)); // alto
        else pos = V2(cx + rng_.range(-0.85f, 0.85f), rng_.range(0.6f, H - 0.05f));                       // mãos
        pos.x = clampf(pos.x, minX, maxX);
        pos.y = std::min(pos.y, top);   // o sensor precisa enxergar o ponto
        bool clash = false;
        for (auto &l : leaks) if (l.state != State::Sealed && (l.pos - pos).len() < 0.40f) clash = true;
        for (int i = 0; i < np && !clash; ++i) if (ps[i]->grid.overlapCircle(pos, 0.18f).cells > 0) clash = true;
        if (clash) continue;
        Leak l;
        l.pos = pos;
        leaks.push_back(l);
        return true;
    }
    return false;
}

void LeaksGame::onUpdate(float dt, const BodyFrame &body, Events &ev) {
    const bool playing = phase() == Phase::Playing;
    for (auto &f : fish) {
        f.x += f.vx * dt;
        f.phase += dt * 3;
        if (f.x > 3.2f) f.vx = -std::fabs(f.vx);
        if (f.x < -3.2f) f.vx = std::fabs(f.vx);
    }
    if (!playing) return;

    const int d = cfg_.difficulty;
    // Quantas rachaduras abertas ao mesmo tempo (aumenta ao longo da partida)
    const float frac = static_cast<float>(spawned) / totalLeaks;
    const int maxOpen = (d == 0 ? 1 : 2) + static_cast<int>(frac * (d == 0 ? 2.f : d == 1 ? 2.5f : 3.5f));
    spawnTimer -= dt;
    if (spawned < totalLeaks && openCount() < maxOpen && spawnTimer <= 0) {
        if (spawnLeak(body)) {
            ++spawned;
            ev.sfx.push_back(Sfx::Crack);
        }
        spawnTimer = d == 0 ? 1.8f : d == 1 ? 1.4f : 1.1f;
    }

    int open = 0;
    for (auto &l : leaks) {
        l.t += dt;
        if (l.state == State::Cracking && l.t > 0.7f) { l.state = State::Open; l.t = 0; }
        if (l.state == State::Sealed) continue;
        bool covered = false;
        for (auto &p : body.players)
            if (p.present && p.grid.overlapCircle(l.pos, l.r).cells >= 2) covered = true;
        // dá para tapar já durante a rachadura (antes de jorrar)
        l.cover = covered ? l.cover + dt : std::max(0.f, l.cover - dt * 2);
        if (l.cover >= 0.25f) {
            l.state = State::Sealed;
            l.sealedAt = playTime();
            addScore(1);
            ev.sfx.push_back(Sfx::Plug);
            recentSeals_.push_back(playTime());
            continue;
        }
        if (l.state == State::Open) ++open;
    }
    // vários tapados quase juntos
    recentSeals_.erase(std::remove_if(recentSeals_.begin(), recentSeals_.end(),
                                      [&](float t) { return playTime() - t > 0.6f; }),
                       recentSeals_.end());
    if (static_cast<float>(recentSeals_.size()) > stats_["maxSimultaneousSeals"]) {
        stat("maxSimultaneousSeals", static_cast<float>(recentSeals_.size()));
        if (recentSeals_.size() == 3) {
            ev.say("Três de uma vez!");
            ev.photos.push_back({"Aquário: três vazamentos tapados!", -1});
        }
    }

    const float rate = d == 0 ? 0.035f : d == 1 ? 0.045f : 0.055f;
    water = std::min(kTankHeight, water + open * rate * dt);
    statMax("maxWaterFrac", water / kTankHeight);
    if (water >= kTankHeight) {
        ev.say("O aquário encheu!", 2.0f);
        finish(ev);
        return;
    }
    if (spawned >= totalLeaks && openCount() == 0) finish(ev);
    if (playTime() > 140) finish(ev);
}

} // namespace k3d::game
