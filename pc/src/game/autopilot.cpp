#include "game/autopilot.h"

#include <algorithm>
#include <cmath>

#include "game/bubbles.h"
#include "game/dodge.h"
#include "game/leaks.h"
#include "game/rebound.h"
#include "game/river.h"

namespace k3d::game {

void Autopilot::moveX(int i, float target, float dt) {
    pose[i].x = approach(pose[i].x, clampf(target, -1.4f, 1.4f), 2.2f * skill_, dt);
}

void Autopilot::animate(int i, float dt) {
    SimPose &p = pose[i];
    if (jumpT_[i] > 0) {
        jumpT_[i] -= dt;
        const float t = 1.f - std::max(0.f, jumpT_[i]) / 0.55f;
        p.jump = 0.38f * std::sin(t * 3.14159f);
    } else {
        p.jump = 0;
    }
    if (flapT_[i] > 0) {
        flapT_[i] -= dt;
        const float t = 1.f - std::max(0.f, flapT_[i]) / 0.55f;  // 0..1
        // desce rápido (0..0.35), sobe devagar (0.35..1)
        const float y = t < 0.35f ? 2.05f - (t / 0.35f) * 1.15f : 0.9f + ((t - 0.35f) / 0.65f) * 1.15f;
        p.handsAuto = false;
        p.leftHand = {p.x - 0.7f, y};
        p.rightHand = {p.x + 0.7f, y};
    }
}

void Autopilot::pointAt(V2 target, float dt) {
    SimPose &p = pose[0];
    p.present = true;
    p.handsAuto = false;
    p.rightHand = target;
    p.leftHand = {p.x - 0.26f, 0.85f};
    t_ += dt;
}

void Autopilot::update(float dt, const MiniGame &g, const BodyFrame &body, int players) {
    t_ += dt;
    for (int i = 0; i < 2; ++i) {
        pose[i].present = i < players;
        if (i == 1 && pose[1].x == 0 && players == 2) pose[1].x = 0.8f;
    }
    if (players == 2 && pose[0].x > -0.3f) pose[0].x = std::min(pose[0].x, -0.5f);

    switch (g.id()) {
        case GameId::River: {
            const auto &r = static_cast<const RiverGame &>(g);
            float targetLat = r.boatLat;
            bool wantJump = false;
            for (const auto &gem : r.gems) {
                if (gem.taken || gem.s < r.boatS + 1) continue;
                if (gem.s > r.boatS + 28) break;
                targetLat = gem.lat;
                const float tArrive = (gem.s - r.boatS) / std::max(1.f, r.speed);
                if (gem.y > 1.3f && tArrive < 0.55f && tArrive > 0.2f) wantJump = true;
                break;
            }
            for (const auto &rk : r.rocks) {
                if (rk.hit || rk.s < r.boatS || rk.s > r.boatS + 14) continue;
                if (std::fabs(rk.lat - targetLat) < rk.r + 1.4f)
                    targetLat = rk.lat + (targetLat >= rk.lat ? 1 : -1) * (rk.r + 1.6f);
            }
            for (const auto &rp : r.ramps) {
                const float tArrive = (rp.s + 1 - r.boatS) / std::max(1.f, r.speed);
                if (tArrive > 0 && tArrive < 0.4f && std::fabs(rp.lat - r.boatLat) < rp.halfWidth) { wantJump = true; targetLat = rp.lat; }
            }
            targetLat = clampf(targetLat, -r.halfWidth + 1.0f, r.halfWidth - 1.0f);
            const float s = clampf((targetLat - r.boatLat) / 1.5f - r.latVel * 0.12f, -1, 1);
            for (int i = 0; i < players; ++i) {
                const float base = players == 2 ? (i == 0 ? -0.45f : 0.45f) : 0.f;
                // em dupla os dois se inclinam juntos; a posição média continua no centro
                moveX(i, base + s * (players == 2 ? 0.f : 0.45f), dt);
                pose[i].lean = approach(pose[i].lean, s * 0.32f, 2.5f, dt);
                pose[i].handsAuto = true;
                if (wantJump && r.onWater) jump(i);
            }
            break;
        }
        case GameId::Rebound: {
            const auto &rb = static_cast<const ReboundGame &>(g);
            for (int i = 0; i < players; ++i) {
                const ReboundGame::Ball *best = nullptr;
                float bestT = 1e9f;
                for (const auto &b : rb.balls) {
                    if (!b.alive || b.vel.z <= 0 || b.pos.z > 0) continue;
                    const float t = -b.pos.z / b.vel.z;
                    float px = b.pos.x + b.vel.x * t;
                    if (players == 2 && ((i == 0) != (px < 0))) continue;  // cada um cuida do seu lado
                    if (t < bestT) { bestT = t; best = &b; }
                }
                SimPose &p = pose[i];
                if (!best) { p.handsAuto = true; moveX(i, players == 2 ? (i == 0 ? -0.6f : 0.6f) : 0.f, dt); continue; }
                float px = best->pos.x + best->vel.x * bestT;
                for (int k = 0; k < 4; ++k) {  // reflexo nas paredes
                    if (px > ReboundGame::kHalfW) px = 2 * ReboundGame::kHalfW - px;
                    if (px < -ReboundGame::kHalfW) px = -2 * ReboundGame::kHalfW - px;
                }
                float py = best->pos.y + best->vel.y * bestT - 2.f * bestT * bestT;
                py = clampf(py, 0.2f, 2.3f);
                moveX(i, px - (px > p.x ? 0.35f : -0.35f), dt);
                p.handsAuto = false;
                if (px >= p.x) { p.rightHand = {px, py}; p.leftHand = {p.x - 0.26f, 0.85f}; }
                else { p.leftHand = {px, py}; p.rightHand = {p.x + 0.26f, 0.85f}; }
                if (py > 1.95f && bestT < 0.3f) jump(i);
            }
            break;
        }
        case GameId::Dodge: {
            const auto &dg = static_cast<const DodgeGame &>(g);
            for (int i = 0; i < players; ++i) {
                SimPose &p = pose[i];
                float wantX = players == 2 ? (i == 0 ? -0.35f : 0.35f) : 0.f;
                p.crouch = approach(p.crouch, 0.f, 4.f, dt);
                p.handsAuto = true;
                for (const auto &o : dg.items) {
                    if (o.resolved) continue;
                    const float tArrive = (-o.z - dg.traveled) / dg.speed;
                    if (tArrive < -0.2f || tArrive > 1.6f) continue;
                    switch (o.kind) {
                        case DodgeGame::Kind::LowBar: if (tArrive < 0.32f && tArrive > 0.0f) jump(i); break;
                        case DodgeGame::Kind::HighBar: if (tArrive < 0.7f) p.crouch = approach(p.crouch, 1.f, 6.f, dt); break;
                        case DodgeGame::Kind::WallLeftGap:
                        case DodgeGame::Kind::WallRightGap: wantX = o.x; break;
                        case DodgeGame::Kind::Gem:
                            if (tArrive < 0.9f) {
                                p.handsAuto = false;
                                const bool right = o.x >= p.x;
                                const float gy = dg.gemY(o);
                                if (right) { p.rightHand = {o.x, gy}; p.leftHand = {p.x - 0.26f, 0.85f}; }
                                else { p.leftHand = {o.x, gy}; p.rightHand = {p.x + 0.26f, 0.85f}; }
                                if (std::fabs(o.x - p.x) > 0.75f) wantX = o.x - (right ? 0.5f : -0.5f);
                            }
                            break;
                    }
                }
                moveX(i, wantX, dt);
            }
            break;
        }
        case GameId::Leaks: {
            const auto &lk = static_cast<const LeaksGame &>(g);
            for (int i = 0; i < players; ++i) {
                SimPose &p = pose[i];
                const LeaksGame::Leak *best = nullptr;
                float bestD = 1e9f;
                for (const auto &l : lk.leaks) {
                    if (l.state == LeaksGame::State::Sealed) continue;
                    if (players == 2 && ((i == 0) != (l.pos.x < 0))) continue;
                    const float d = std::fabs(l.pos.x - p.x) + l.t * -0.2f;
                    if (d < bestD) { bestD = d; best = &l; }
                }
                p.handsAuto = true;
                if (!best) continue;
                if (best->pos.y < 0.3f) {
                    // pé: coloca o pé mais próximo sobre a rachadura
                    moveX(i, best->pos.x + (best->pos.x > p.x ? -0.13f : 0.13f), dt);
                } else {
                    if (std::fabs(best->pos.x - p.x) > 0.42f) moveX(i, best->pos.x - (best->pos.x > p.x ? 0.35f : -0.35f), dt);
                    p.handsAuto = false;
                    if (best->pos.x >= p.x) { p.rightHand = best->pos; p.leftHand = {p.x - 0.26f, 0.85f}; }
                    else { p.leftHand = best->pos; p.rightHand = {p.x + 0.26f, 0.85f}; }
                    // segunda rachadura na outra mão
                    for (const auto &l : lk.leaks) {
                        if (&l == best || l.state == LeaksGame::State::Sealed || l.pos.y < 0.3f) continue;
                        if (best->pos.x >= p.x && l.pos.x < p.x && l.pos.x > p.x - 0.9f) p.leftHand = l.pos;
                        if (best->pos.x < p.x && l.pos.x >= p.x && l.pos.x < p.x + 0.9f) p.rightHand = l.pos;
                    }
                }
            }
            break;
        }
        case GameId::Bubbles: {
            const auto &bb = static_cast<const BubblesGame &>(g);
            for (int i = 0; i < players; ++i) {
                SimPose &p = pose[i];
                const auto &f = bb.floaters[i];
                const BubblesGame::Bubble *best = nullptr;
                float bestD = 1e9f;
                for (const auto &b : bb.bubbles) {
                    if (b.popped) continue;
                    const float d = std::fabs(b.pos.x - f.x) * 0.8f + std::fabs(b.pos.y - (f.y + 1.2f)) + b.life * 0.05f;
                    if (d < bestD) { bestD = d; best = &b; }
                }
                if (flapT_[i] <= 0) p.handsAuto = true;
                if (!best) continue;
                const float wantFloat = best->pos.y - 1.25f;
                if (f.y < wantFloat - 0.15f && f.vy < 1.0f) flap(i);
                // x do corpo para que o boneco (ampliado 1,9x) chegue à bolha
                moveX(i, clampf(best->pos.x / 1.9f, -1.3f, 1.3f), dt);
                p.lean = 0;
                if (flapT_[i] <= 0) {
                    p.handsAuto = false;
                    const V2 off(f.x - p.x, f.y);
                    const V2 target = best->pos - off;
                    if (target.x >= p.x) { p.rightHand = target; p.leftHand = {p.x - 0.26f, 0.85f}; }
                    else { p.leftHand = target; p.rightHand = {p.x + 0.26f, 0.85f}; }
                }
            }
            break;
        }
    }
    for (int i = 0; i < players; ++i) animate(i, dt);
    (void)body;
}

} // namespace k3d::game
