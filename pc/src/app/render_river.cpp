#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/render_games.h"
#include "game/river.h"
#include "raymath.h"
#include "rlgl.h"

namespace k3d {

// ----------------------------------------------------------- utilidades comuns

Color mixColor(Color a, Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return Color{static_cast<unsigned char>(a.r + (b.r - a.r) * t), static_cast<unsigned char>(a.g + (b.g - a.g) * t),
                 static_cast<unsigned char>(a.b + (b.b - a.b) * t), static_cast<unsigned char>(a.a + (b.a - a.a) * t)};
}

void quad3D(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color ca, Color cb, Color cc, Color cd) {
    rlBegin(RL_TRIANGLES);
    rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex3f(a.x, a.y, a.z);
    rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlVertex3f(b.x, b.y, b.z);
    rlColor4ub(cc.r, cc.g, cc.b, cc.a); rlVertex3f(c.x, c.y, c.z);
    rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex3f(a.x, a.y, a.z);
    rlColor4ub(cc.r, cc.g, cc.b, cc.a); rlVertex3f(c.x, c.y, c.z);
    rlColor4ub(cd.r, cd.g, cd.b, cd.a); rlVertex3f(d.x, d.y, d.z);
    rlEnd();
}

void drawBoxRotX(Vector3 p, Vector3 s, float degX, Color c) {
    Model &m = world3d().cube;
    m.transform = MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixRotateX(degX * DEG2RAD)),
                                 MatrixTranslate(p.x, p.y, p.z));
    DrawModel(m, {0, 0, 0}, 1, c);
}

void Particles::burst(Vector3 at, int n, Color c, float speed, float size, float life, float gravity, unsigned seed) {
    unsigned s = seed * 2654435761u + static_cast<unsigned>(list.size()) * 7919u + 1;
    auto rnd = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFF) / 65535.f; };
    for (int i = 0; i < n; ++i) {
        const float a = rnd() * 6.2832f, e = rnd() * 1.4f - 0.2f;
        const Vector3 v{std::cos(a) * std::cos(e) * speed * (0.4f + rnd()), std::sin(e) * speed * (0.6f + rnd()),
                        std::sin(a) * std::cos(e) * speed * (0.4f + rnd())};
        list.push_back({at, v, life * (0.6f + 0.4f * rnd()), life, size * (0.6f + 0.6f * rnd()), c, gravity});
    }
    if (list.size() > 900) list.erase(list.begin(), list.begin() + (list.size() - 900));
}

void Particles::update(float dt) {
    for (auto &p : list) {
        p.v.y -= p.gravity * dt;
        p.p = Vector3Add(p.p, Vector3Scale(p.v, dt));
        p.life -= dt;
    }
    list.erase(std::remove_if(list.begin(), list.end(), [](const Particle &p) { return p.life <= 0; }), list.end());
}

void Particles::draw() const {
    for (const auto &p : list) {
        Color c = p.c;
        c.a = static_cast<unsigned char>(c.a * std::clamp(p.life / p.maxLife, 0.f, 1.f));
        DrawCube(p.p, p.size, p.size, p.size, c);
    }
}

// ------------------------------------------------------------------ Corredeira

namespace {
float hash(float x) { const float s = std::sin(x * 12.9898f) * 43758.5453f; return s - std::floor(s); }
}

void renderRiver(App &app, const game::RiverGame &g, const RenderInfo &ri, RenderState &rs) {
    const float t = ri.time;
    const Color skyTop{110, 180, 245, 255}, skyBot{205, 232, 250, 255};
    const Color fog = skyBot;
    drawSky(ri.width, ri.height, skyTop, skyBot);

    auto W = [&](float s, float lat, float y) { return Vector3{g.centerX(s) + lat, y, -s}; };
    const Vector3 B = W(g.boatS, g.boatLat, g.boatY);
    Camera3D cam{};
    cam.position = {B.x - g.latVel * 0.15f, 4.4f + g.boatY * 0.4f, B.z + 8.0f};
    cam.target = {B.x, 0.8f + g.boatY * 0.5f, B.z - 12.f};
    cam.up = {0, 1, 0};
    cam.fovy = 55;
    cam.projection = CAMERA_PERSPECTIVE;
    cam = shakeCamera(cam, ri.shake, t);

    World3D &w = world3d();
    w.begin(cam, fog, 35, 150, Vector3Normalize({-0.4f, -1.f, -0.5f}), 0.5f);

    // montanhas ao longe
    for (int k = -4; k <= 4; ++k)
        drawCone({g.centerX(g.boatS + 150) + k * 34.f, -3, B.z - 160 + static_cast<float>(std::abs(k)) * 6.f}, 26, 40 + 10 * hash(k * 3.1f),
                 Color{96, 150, 120, 255});

    // água e margens
    const bool canyon = g.config().difficulty >= 1;
    const Color bank = canyon ? Color{176, 140, 104, 255} : Color{96, 178, 92, 255};
    const float s0 = std::floor(g.boatS / 3.f) * 3.f - 12;
    for (float s = s0; s < g.boatS + 140; s += 3) {
        const float s1 = s + 3;
        const float hw = g.halfWidth;
        const float d = std::max(0.f, s - g.boatS);
        const float f = std::clamp((d - 35) / 115.f, 0.f, 1.f);
        const float band = 0.5f + 0.5f * std::sin(s * 0.45f + t * 3.0f);
        const Color water = mixColor(mixColor(Color{38, 128, 196, 255}, Color{70, 168, 222, 255}, band), fog, f);
        const Color bankC = mixColor(bank, fog, f);
        const Color edge = mixColor(Color{220, 240, 255, 255}, fog, f);
        quad3D(W(s, -hw, 0), W(s, hw, 0), W(s1, hw, 0), W(s1, -hw, 0), water, water, water, water);
        // espuma nas bordas
        quad3D(W(s, -hw - 0.3f, 0.02f), W(s, -hw + 0.4f, 0.02f), W(s1, -hw + 0.4f, 0.02f), W(s1, -hw - 0.3f, 0.02f), edge, water, water, edge);
        quad3D(W(s, hw - 0.4f, 0.02f), W(s, hw + 0.3f, 0.02f), W(s1, hw + 0.3f, 0.02f), W(s1, hw - 0.4f, 0.02f), water, edge, edge, water);
        // margens elevadas
        const float bh = canyon ? 0.6f : 0.35f;
        quad3D(W(s, -hw - 14, bh), W(s, -hw, bh), W(s1, -hw, bh), W(s1, -hw - 14, bh), bankC, bankC, bankC, bankC);
        quad3D(W(s, hw, bh), W(s, hw + 14, bh), W(s1, hw + 14, bh), W(s1, hw, bh), bankC, bankC, bankC, bankC);
        const Color side = mixColor(Color{120, 92, 60, 255}, fog, f);
        quad3D(W(s, -hw, 0), W(s, -hw, bh), W(s1, -hw, bh), W(s1, -hw, 0), side, side, side, side);
        quad3D(W(s, hw, bh), W(s, hw, 0), W(s1, hw, 0), W(s1, hw, bh), side, side, side, side);
    }
    // árvores ou paredões
    for (float s = std::floor(g.boatS / 7.f) * 7.f - 7; s < g.boatS + 130; s += 7) {
        for (int side = -1; side <= 1; side += 2) {
            const float h = hash(s * 0.37f + side);
            const float lat = side * (g.halfWidth + 2.5f + h * 6.f);
            const Vector3 p = W(s + h * 3, lat, canyon ? 0.6f : 0.35f);
            if (canyon) {
                drawBox({p.x, p.y + 3 + h * 3, p.z}, {3 + h * 2, 6 + h * 6, 4}, Color{168, 120, 88, 255});
            } else {
                drawCylinderY(p, 0.25f, 1.6f, Color{110, 76, 50, 255});
                drawCone({p.x, p.y + 1.2f, p.z}, 1.4f + h * 0.6f, 3.0f + h * 1.5f, Color{46, 140, 70, 255});
            }
        }
    }
    // rampas
    for (const auto &r : g.ramps) {
        if (r.s < g.boatS - 6 || r.s > g.boatS + 130) continue;
        const Vector3 p = W(r.s + r.length / 2, r.lat, 0.35f);
        drawBoxRotX(p, {r.halfWidth * 2, 0.15f, r.length + 0.6f}, -9.f, Color{196, 140, 72, 255});
        drawBox(W(r.s + r.length, r.lat - r.halfWidth, 0.4f), {0.15f, 0.8f, 0.15f}, Color{255, 200, 40, 255});
        drawBox(W(r.s + r.length, r.lat + r.halfWidth, 0.4f), {0.15f, 0.8f, 0.15f}, Color{255, 200, 40, 255});
    }
    // pedras
    for (const auto &r : g.rocks) {
        if (r.s < g.boatS - 6 || r.s > g.boatS + 130) continue;
        const Vector3 p = W(r.s, r.lat, 0.05f);
        drawEllipsoid(p, {r.r, r.r * 0.75f, r.r * 0.9f}, r.hit ? Color{120, 110, 105, 255} : Color{140, 136, 132, 255});
        drawEllipsoid({p.x, 0.02f, p.z}, {r.r + 0.35f, 0.03f, r.r + 0.45f}, Color{235, 245, 255, 255});
    }
    // anéis
    for (const auto &r : g.rings) {
        if (r.s < g.boatS - 3 || r.s > g.boatS + 130) continue;
        drawTorus(W(r.s, r.lat, r.y), 1.25f, {0, 1, 0}, 0, r.passed ? Color{140, 255, 160, 255} : Color{255, 90, 160, 255});
    }
    // cristais
    for (const auto &gm : g.gems) {
        if (gm.taken || gm.s < g.boatS - 3) continue;
        if (gm.s > g.boatS + 110) break;
        const Vector3 p = W(gm.s, gm.lat, gm.y + 0.25f + 0.15f * std::sin(t * 3 + gm.s));
        drawGem(p, 0.45f, t * 2.5f + gm.s, gm.y > 1.3f ? Color{255, 210, 60, 255} : Color{80, 230, 255, 255});
    }
    // chegada
    if (g.length - g.boatS < 140) {
        const float s = g.length;
        for (int side = -1; side <= 1; side += 2) drawCylinderY(W(s, side * (g.halfWidth + 0.3f), 0), 0.25f, 5.5f, Color{240, 240, 240, 255});
        for (int i = 0; i < 12; ++i) {
            const float lat = -g.halfWidth + (i + 0.5f) * (2 * g.halfWidth / 12);
            drawBox(W(s, lat, 5.2f), {2 * g.halfWidth / 12, 0.7f, 0.2f}, (i % 2) ? Color{30, 30, 30, 255} : Color{250, 250, 250, 255});
        }
    }
    // bote
    const float bob = std::sin(t * 4.f) * 0.05f;
    const Vector3 bp{B.x, B.y + 0.18f + bob, B.z};
    drawEllipsoid(bp, {1.25f, 0.32f, 1.6f}, Color{255, 120, 40, 255});
    drawEllipsoid({bp.x, bp.y + 0.12f, bp.z}, {1.0f, 0.2f, 1.35f}, Color{255, 190, 90, 255});
    w.end();

    // respingos
    if (rs.prevFlags.size() < 2) rs.prevFlags.assign(2, 1);
    const bool landed = g.onWater && !rs.prevFlags[0];
    const bool bumped = g.bumpTimer > 0 && !rs.prevFlags[1];
    if (landed || bumped) rs.fx.burst({bp.x, 0.2f, bp.z}, 40, Color{230, 245, 255, 230}, 4.f, 0.12f, 0.8f, 8.f, rs.seed++);
    rs.prevFlags[0] = g.onWater;
    rs.prevFlags[1] = g.bumpTimer > 0;
    if (g.onWater && g.speed > 2) rs.fx.burst({bp.x + std::sin(t * 9) * 0.8f, 0.1f, bp.z + 1.6f}, 1, Color{235, 248, 255, 200}, 1.2f, 0.09f, 0.6f, 3.f, rs.seed++);
    if (g.score() > rs.prevScore) rs.fx.burst({bp.x, bp.y + 1.2f, bp.z - 1}, 14, Color{255, 230, 90, 255}, 3.f, 0.08f, 0.6f, 2.f, rs.seed++);
    rs.prevScore = g.score();

    BeginMode3D(cam);
    rs.fx.draw();
    // jogadores no bote (silhueta medida pelo Kinect, em escala)
    rlDisableDepthMask();
    for (int i = 0; i < 2; ++i) app.silhouettes.draw3D(i, {bp.x, bp.y + 0.25f, bp.z + 0.2f}, 0.62f, 0.95f);
    rlEnableDepthMask();
    EndMode3D();
}

} // namespace k3d
