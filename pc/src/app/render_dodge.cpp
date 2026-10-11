#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/render_games.h"
#include "game/dodge.h"
#include "raymath.h"
#include "rlgl.h"

namespace k3d {

void renderDodge(App &app, const game::DodgeGame &g, const RenderInfo &ri, RenderState &rs) {
    using DG = game::DodgeGame;
    const float t = ri.time;
    const Color skyTop{92, 120, 220, 255}, skyBot{250, 190, 160, 255};
    drawSky(ri.width, ri.height, skyTop, skyBot);
    Camera3D cam{};
    cam.position = {0, 1.95f, 4.3f};
    cam.target = {0, 1.2f, -10};
    cam.up = {0, 1, 0};
    cam.fovy = 58;
    cam.projection = CAMERA_PERSPECTIVE;
    cam = shakeCamera(cam, ri.shake + g.stumble, t);
    World3D &w = world3d();
    const Color fog{240, 196, 180, 255};
    w.begin(cam, fog, 25, 90, Vector3Normalize({0.5f, -1.f, -0.3f}), 0.5f);

    // montanhas e nuvens
    for (int k = -5; k <= 5; ++k)
        drawCone({k * 22.f, -25, -110.f - (k % 2) * 12}, 18, 45 + (k * 7 % 13), Color{120, 110, 170, 255});
    for (int k = 0; k < 10; ++k) {
        const float z = -std::fmod(k * 13.f + g.traveled * 0.6f, 120.f) - 10;
        const float x = (k % 2 ? -1 : 1) * (8 + (k * 37 % 11));
        drawEllipsoid({x, -3.f + (k % 3), z}, {4.5f, 1.2f, 2.5f}, Color{255, 245, 245, 255});
    }
    // trilhos e dormentes
    drawBox({-1.6f, -0.15f, -30}, {0.12f, 0.12f, 70}, Color{180, 190, 210, 255});
    drawBox({1.6f, -0.15f, -30}, {0.12f, 0.12f, 70}, Color{180, 190, 210, 255});
    const float off = std::fmod(g.traveled, 1.5f);
    for (int i = 0; i < 40; ++i) drawBox({0, -0.25f, 2.f - i * 1.5f + off}, {3.6f, 0.08f, 0.3f}, Color{120, 84, 60, 255});
    // plataforma do jogador
    drawBox({0, -0.1f, 0.3f}, {3.4f, 0.16f, 2.6f}, Color{70, 90, 120, 255});
    drawBox({0, -0.01f, 0.3f}, {3.2f, 0.02f, 2.4f}, Color{100, 130, 170, 255});

    // obstáculos
    for (const auto &o : g.items) {
        const float d = -o.z - g.traveled;   // distância à frente
        if (d < -4 || d > 70) continue;
        const float z = -d;
        switch (o.kind) {
            case DG::Kind::LowBar:
                for (int i = 0; i < 8; ++i)
                    drawBox({-1.75f + i * 0.5f, 0.14f, z}, {0.5f, 0.14f, 0.14f}, i % 2 ? Color{250, 250, 250, 255} : Color{230, 50, 60, 255});
                drawBox({-2.0f, 0.1f, z}, {0.1f, 0.25f, 0.1f}, Color{80, 80, 90, 255});
                drawBox({2.0f, 0.1f, z}, {0.1f, 0.25f, 0.1f}, Color{80, 80, 90, 255});
                break;
            case DG::Kind::HighBar: {
                const float y0 = DG::kHighBarBottom * g.refHeight;
                for (int i = 0; i < 8; ++i)
                    drawBox({-1.75f + i * 0.5f, y0 + 0.17f, z}, {0.5f, 0.34f, 0.18f}, i % 2 ? Color{30, 30, 40, 255} : Color{255, 205, 40, 255});
                drawBox({-2.05f, (y0 + 0.34f) / 2, z}, {0.12f, y0 + 0.34f, 0.12f}, Color{90, 90, 100, 255});
                drawBox({2.05f, (y0 + 0.34f) / 2, z}, {0.12f, y0 + 0.34f, 0.12f}, Color{90, 90, 100, 255});
                break;
            }
            case DG::Kind::WallLeftGap:
            case DG::Kind::WallRightGap: {
                const float g0 = o.x - o.param * 0.5f, g1 = o.x + o.param * 0.5f;
                const Color c = o.hit ? Color{200, 80, 120, 255} : Color{140, 100, 230, 255};
                if (g0 > -2.1f) drawBox({(-2.1f + g0) / 2, 1.3f, z}, {g0 + 2.1f, 2.6f, 0.25f}, c);
                if (g1 < 2.1f) drawBox({(g1 + 2.1f) / 2, 1.3f, z}, {2.1f - g1, 2.6f, 0.25f}, c);
                // moldura da abertura
                drawBox({g0, 1.3f, z + 0.14f}, {0.06f, 2.6f, 0.04f}, Color{120, 255, 230, 255});
                drawBox({g1, 1.3f, z + 0.14f}, {0.06f, 2.6f, 0.04f}, Color{120, 255, 230, 255});
                break;
            }
            case DG::Kind::Gem:
                if (!o.taken) drawGem({o.x, g.gemY(o), z}, 0.32f, t * 2.5f + o.z, Color{80, 230, 255, 255});
                break;
        }
    }
    w.end();

    if (g.score() > rs.prevScore) rs.fx.burst({0, 1.4f, -0.3f}, 16, Color{120, 240, 255, 255}, 3.f, 0.07f, 0.6f, 2.f, rs.seed++);
    rs.prevScore = g.score();
    BeginMode3D(cam);
    rs.fx.draw();
    rlDisableDepthMask();
    const float blink = (g.invuln[0] > 0 && std::fmod(t, 0.2f) < 0.1f) ? 0.4f : 0.92f;
    for (int i = 0; i < 2; ++i) app.silhouettes.draw3D(i, {0, 0, 0}, 1.f, blink);
    rlEnableDepthMask();
    EndMode3D();
}

} // namespace k3d
