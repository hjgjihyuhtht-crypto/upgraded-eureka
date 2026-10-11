#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/render_games.h"
#include "game/leaks.h"
#include "raymath.h"
#include "rlgl.h"

namespace k3d {

void renderLeaks(App &app, const game::LeaksGame &g, const RenderInfo &ri, RenderState &rs) {
    using LG = game::LeaksGame;
    const float t = ri.time;
    drawSky(ri.width, ri.height, Color{10, 60, 110, 255}, Color{4, 22, 48, 255});
    Camera3D cam{};
    cam.position = {0, 1.35f, 3.7f};
    cam.target = {0, 1.15f, -1};
    cam.up = {0, 1, 0};
    cam.fovy = 62;
    cam.projection = CAMERA_PERSPECTIVE;
    cam = shakeCamera(cam, ri.shake, t);
    World3D &w = world3d();
    const Color fog{8, 50, 96, 255};
    w.begin(cam, fog, 4, 14, Vector3Normalize({0.2f, -1.f, -0.4f}), 0.5f);

    const float glassZ = -0.5f;
    // mar lá fora: areia, algas, pedras e peixes
    for (int ix = -6; ix <= 6; ++ix)
        for (int iz = 0; iz < 8; ++iz) {
            const Color c = ((ix + iz) % 2) ? Color{194, 170, 120, 255} : Color{178, 156, 110, 255};
            quad3D({ix * 1.f, -0.02f, glassZ - iz * 1.f}, {ix + 1.f, -0.02f, glassZ - iz * 1.f},
                   {ix + 1.f, -0.02f, glassZ - iz - 1.f}, {ix * 1.f, -0.02f, glassZ - iz - 1.f}, c, c, c, c);
        }
    for (int k = 0; k < 9; ++k) {
        const float x = -4 + k * 1.0f + std::sin(k * 7.3f) * 0.4f, z = -2.5f - (k % 3) * 1.2f;
        const float sway = std::sin(t * 1.3f + k) * 0.2f;
        drawCone({x + sway * 0.3f, 0, z}, 0.18f, 1.2f + (k % 4) * 0.4f, Color{40, 160, 90, 255});
        if (k % 3 == 0) drawEllipsoid({x + 0.6f, 0.15f, z - 0.5f}, {0.5f, 0.3f, 0.4f}, Color{110, 110, 130, 255});
    }
    const Color fishCols[5] = {Color{255, 140, 40, 255}, Color{255, 220, 60, 255}, Color{90, 200, 255, 255},
                               Color{255, 100, 160, 255}, Color{160, 255, 140, 255}};
    for (const auto &f : g.fish) {
        const float dir = f.vx > 0 ? 1.f : -1.f;
        const Vector3 p{f.x, f.y + std::sin(f.phase) * 0.08f, glassZ - 1.6f};
        drawEllipsoid(p, {0.28f, 0.14f, 0.08f}, fishCols[f.color % 5]);
        drawEllipsoid({p.x - dir * 0.3f, p.y, p.z}, {0.1f, 0.12f + 0.03f * std::sin(f.phase * 3), 0.03f}, ColorBrightness(fishCols[f.color % 5], -0.2f));
        drawSphere({p.x + dir * 0.17f, p.y + 0.04f, p.z + 0.06f}, 0.025f, Color{20, 20, 30, 255});
    }
    // sala: chão, molduras
    for (int ix = -3; ix < 3; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            drawBox({ix * 0.75f + 0.375f, -0.05f, glassZ + 0.375f + iz * 0.75f}, {0.73f, 0.1f, 0.73f},
                    ((ix + iz) % 2) ? Color{220, 230, 240, 255} : Color{196, 210, 226, 255});
    for (float x : {-2.25f, -0.75f, 0.75f, 2.25f}) drawBox({x, 1.5f, glassZ}, {0.08f, 3.0f, 0.08f}, Color{160, 170, 190, 255});
    for (float y : {0.f, 1.5f, 3.0f}) drawBox({0, y, glassZ}, {4.6f, 0.08f, 0.08f}, Color{160, 170, 190, 255});
    w.end();

    BeginMode3D(cam);
    // vidro
    quad3D({-2.3f, 0, glassZ}, {2.3f, 0, glassZ}, {2.3f, 3.0f, glassZ}, {-2.3f, 3.0f, glassZ}, Color{180, 230, 255, 30},
           Color{180, 230, 255, 30}, Color{220, 245, 255, 50}, Color{220, 245, 255, 50});
    // rachaduras, jatos e remendos
    for (const auto &l : g.leaks) {
        const Vector3 c{l.pos.x, l.pos.y, glassZ + 0.01f};
        if (l.state == LG::State::Sealed) {
            DrawCube({c.x, c.y, c.z + 0.02f}, 0.30f, 0.30f, 0.02f, Color{255, 190, 60, 255});
            DrawCube({c.x, c.y, c.z + 0.035f}, 0.32f, 0.07f, 0.01f, Color{255, 240, 200, 255});
            DrawCube({c.x, c.y, c.z + 0.035f}, 0.07f, 0.32f, 0.01f, Color{255, 240, 200, 255});
            continue;
        }
        const float grow = l.state == LG::State::Cracking ? std::min(1.f, l.t / 0.7f) : 1.f;
        for (int k = 0; k < 7; ++k) {
            const float a = k * 0.9f + l.pos.x * 3;
            const float len = (0.12f + 0.1f * std::fabs(std::sin(k * 2.1f + l.pos.y * 5))) * grow;
            const Vector3 e{c.x + std::cos(a) * len, c.y + std::sin(a) * len, c.z};
            DrawLine3D(c, e, Color{255, 255, 255, 230});
            DrawLine3D(e, {e.x + std::cos(a + 0.6f) * len * 0.5f, e.y + std::sin(a + 0.6f) * len * 0.5f, c.z}, Color{255, 255, 255, 200});
        }
        if (l.state == LG::State::Cracking) {
            const float pulse = 0.5f + 0.5f * std::sin(t * 18);
            DrawSphereEx(c, 0.05f + 0.03f * pulse, 6, 8, Color{255, 80, 80, 220});
        } else {
            // jato de água em direção ao jogador
            if (static_cast<int>(t * 60) % 2 == 0)
                rs.fx.burst(c, 3, Color{120, 200, 255, 220}, 1.8f, 0.05f, 0.7f, 6.f, rs.seed++);
            DrawSphereEx(c, 0.07f, 6, 8, Color{150, 220, 255, 230});
        }
    }
    rs.fx.draw();
    rlDisableDepthMask();
    for (int i = 0; i < 2; ++i) app.silhouettes.draw3D(i, {0, 0, -0.15f}, 1.f, 0.9f);
    // água subindo (translúcida, por cima das pernas)
    if (g.water > 0.01f) {
        const float y = g.water + std::sin(t * 2) * 0.02f;
        const Color wc{40, 140, 230, 110}, top{140, 210, 255, 150};
        quad3D({-2.3f, 0, glassZ}, {2.3f, 0, glassZ}, {2.3f, y, glassZ}, {-2.3f, y, glassZ}, wc, wc, wc, wc);
        quad3D({-2.3f, 0, 2.6f}, {2.3f, 0, 2.6f}, {2.3f, y, 2.6f}, {-2.3f, y, 2.6f}, wc, wc, wc, wc);
        quad3D({-2.3f, y, 2.6f}, {2.3f, y, 2.6f}, {2.3f, y, glassZ}, {-2.3f, y, glassZ}, top, top, top, top);
    }
    rlEnableDepthMask();
    EndMode3D();

    // medidor de água
    const float s = app.ui.s();
    const Rectangle bar{ri.width - 60 * s, 140 * s, 26 * s, ri.height - 260 * s};
    DrawRectangleRounded(bar, 0.5f, 6, Color{0, 0, 0, 120});
    const float f = g.water / LG::kTankHeight;
    DrawRectangleRounded({bar.x + 4 * s, bar.y + bar.height * (1 - f), bar.width - 8 * s, bar.height * f}, 0.5f, 6,
                         f > 0.7f ? Color{255, 90, 90, 230} : Color{80, 180, 255, 230});
    app.ui.text("Água", bar.x + bar.width / 2, bar.y - 30 * s, 20 * s, theme::text, Align::Center, true);
}

} // namespace k3d
