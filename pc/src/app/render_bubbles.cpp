#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/render_games.h"
#include "game/bubbles.h"
#include "raymath.h"
#include "rlgl.h"

namespace k3d {

void renderBubbles(App &app, const game::BubblesGame &g, const RenderInfo &ri, RenderState &rs) {
    using BG = game::BubblesGame;
    const float t = ri.time;
    drawSky(ri.width, ri.height, Color{30, 14, 60, 255}, Color{8, 10, 30, 255});
    // estrelas
    for (int i = 0; i < 120; ++i) {
        const float x = std::fmod(i * 97.13f, static_cast<float>(ri.width));
        const float y = std::fmod(i * 53.71f + i * i * 0.37f, static_cast<float>(ri.height));
        const unsigned char a = static_cast<unsigned char>(120 + 100 * std::sin(t * 2 + i));
        DrawPixelV({x, y}, Color{255, 255, 255, a});
        if (i % 9 == 0) DrawCircleV({x, y}, 1.5f, Color{200, 220, 255, a});
    }
    float avg = 0; int n = 0;
    for (int i = 0; i < 2; ++i) if (app.body().players[i].present) { avg += g.floaters[i].y; ++n; }
    avg = n ? avg / n : g.floaters[0].y;
    static float camY = 1.1f;
    camY += (1.1f + avg - camY) * 0.08f;
    Camera3D cam{};
    cam.position = {0, camY + 0.5f, 6.6f};
    cam.target = {0, camY, -1};
    cam.up = {0, 1, 0};
    cam.fovy = 56;
    cam.projection = CAMERA_PERSPECTIVE;
    cam = shakeCamera(cam, ri.shake, t);
    World3D &w = world3d();
    const Color fog{14, 12, 40, 255};
    w.begin(cam, fog, 8, 26, Vector3Normalize({0.3f, -0.8f, -0.6f}), 0.55f);
    // parede do fundo com painéis
    for (int ix = -3; ix < 3; ++ix)
        for (int iy = 0; iy < 4; ++iy)
            drawBox({ix * 1.0f + 0.5f, iy * 2.0f + 1.0f, -1.6f}, {0.94f, 1.9f, 0.1f},
                    ((ix + iy) % 3 == 0) ? Color{40, 34, 90, 255} : Color{30, 26, 70, 255});
    drawBox({0, -0.05f, 0.5f}, {2 * BG::kRoomHalfW + 0.4f, 0.1f, 4.2f}, Color{36, 40, 80, 255});
    w.end();

    BeginMode3D(cam);
    // contornos neon da sala e marcas de altura
    const Color neon{120, 230, 255, 255};
    for (float x : {-BG::kRoomHalfW, BG::kRoomHalfW}) {
        DrawLine3D({x, 0, -1.5f}, {x, BG::kRoomH, -1.5f}, neon);
    }
    DrawLine3D({-BG::kRoomHalfW, BG::kRoomH, -1.5f}, {BG::kRoomHalfW, BG::kRoomH, -1.5f}, neon);
    for (int m = 1; m <= 7; ++m) DrawLine3D({-BG::kRoomHalfW, static_cast<float>(m), -1.5f}, {-BG::kRoomHalfW + 0.3f, static_cast<float>(m), -1.5f}, neon);
    // bolhas
    const Color cols[4] = {Color{110, 225, 255, 215}, Color{255, 130, 215, 215}, Color{140, 255, 170, 215}, Color{190, 150, 255, 215}};
    for (size_t i = 0; i < g.bubbles.size(); ++i) {
        const auto &b = g.bubbles[i];
        const Vector3 p{b.pos.x, b.pos.y, 0};
        if (b.popped) {
            const float k = b.popT / 0.4f;
            DrawCircle3D(p, b.r * (1 + k * 1.5f), {0, 1, 0}, 0, Color{255, 255, 255, static_cast<unsigned char>(220 * (1 - k))});
            continue;
        }
        const Color c = b.value > 1 ? Color{255, 210, 60, 235} : cols[(static_cast<int>(b.pos.x * 10) + static_cast<int>(i)) & 3];
        const float fade = b.life < 1.f ? b.life : 1.f;
        Color cc = c; cc.a = static_cast<unsigned char>(c.a * fade);
        DrawSphereEx(p, b.r, 12, 16, cc);
        DrawCircle3D(p, b.r * 1.02f, {0, 1, 0}, 0, Color{255, 255, 255, static_cast<unsigned char>(160 * fade)});
        DrawSphereEx({p.x - b.r * 0.35f, p.y + b.r * 0.35f, p.z + b.r * 0.6f}, b.r * 0.2f, 6, 8, Color{255, 255, 255, static_cast<unsigned char>(200 * fade)});
    }
    if (g.score() > rs.prevScore) rs.fx.burst({0, avg + 1.2f, 0}, 10, Color{255, 255, 255, 255}, 2.f, 0.05f, 0.5f, 0.f, rs.seed++);
    rs.prevScore = g.score();
    // impulso ao bater os braços
    for (int i = 0; i < 2; ++i) {
        const PlayerBody &p = app.body().players[i];
        if (p.present && g.floaters[i].flapGlow > 0.9f)
            rs.fx.burst({g.floaters[i].x, g.floaters[i].y + 0.1f, 0}, 18, Color{120, 230, 255, 255}, 2.5f, 0.07f, 0.6f, 4.f, rs.seed++);
    }
    rs.fx.draw();
    rlDisableDepthMask();
    for (int i = 0; i < 2; ++i) {
        const PlayerBody &p = app.body().players[i];
        if (!p.present) continue;
        app.silhouettes.draw3D(i, {0, g.floaters[i].y, 0.05f}, 1.f, 0.92f, g.floaters[i].x - p.x);
    }
    rlEnableDepthMask();
    EndMode3D();
}

} // namespace k3d
