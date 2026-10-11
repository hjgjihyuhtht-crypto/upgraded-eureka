#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/render_games.h"
#include "game/rebound.h"
#include "raymath.h"
#include "rlgl.h"

namespace k3d {

void renderRebound(App &app, const game::ReboundGame &g, const RenderInfo &ri, RenderState &rs) {
    using RG = game::ReboundGame;
    const float t = ri.time;
    drawSky(ri.width, ri.height, Color{26, 20, 52, 255}, Color{60, 40, 90, 255});
    Camera3D cam{};
    cam.position = {0, 2.3f, 3.8f};
    cam.target = {0, 1.0f, -8};
    cam.up = {0, 1, 0};
    cam.fovy = 54;
    cam.projection = CAMERA_PERSPECTIVE;
    cam = shakeCamera(cam, ri.shake, t);
    World3D &w = world3d();
    const Color fog{40, 30, 70, 255};
    w.begin(cam, fog, 9, 24, Vector3Normalize({0.3f, -1.f, -0.6f}), 0.55f);

    // corredor
    const float hw = RG::kHalfW, H = RG::kHeight, zEnd = RG::kTargetZ - 0.4f;
    for (int iz = 0; iz < 30; ++iz) {
        const float z0 = 2.0f - iz * 0.6f, z1 = z0 - 0.6f;
        if (z1 < zEnd) break;
        for (int ix = 0; ix < 6; ++ix) {
            const float x0 = -hw + ix * 0.6f, x1 = x0 + 0.6f;
            const bool dark = (ix + iz) % 2;
            const float f = std::clamp((-z0) / 18.f, 0.f, 1.f);
            const Color c = mixColor(dark ? Color{70, 60, 130, 255} : Color{96, 84, 170, 255}, fog, f * 0.7f);
            quad3D({x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1}, {x0, 0, z1}, c, c, c, c);
        }
        // paredes com faixas
        const Color wl = mixColor((iz % 4 < 2) ? Color{60, 170, 200, 255} : Color{40, 120, 170, 255}, fog, std::clamp(-z0 / 18.f, 0.f, 1.f) * 0.7f);
        quad3D({-hw, 0, z1}, {-hw, 0, z0}, {-hw, H, z0}, {-hw, H, z1}, wl, wl, wl, wl);
        quad3D({hw, 0, z0}, {hw, 0, z1}, {hw, H, z1}, {hw, H, z0}, wl, wl, wl, wl);
    }
    // vigas do teto
    for (float z = 0.5f; z > zEnd; z -= 2.0f) {
        drawBox({-hw + 0.04f, H - 0.02f, z}, {0.08f, 0.04f, 1.2f}, Color{255, 230, 160, 255});
        drawBox({hw - 0.04f, H - 0.02f, z}, {0.08f, 0.04f, 1.2f}, Color{255, 230, 160, 255});
    }
    // fundo
    drawBox({0, H / 2, zEnd - 0.3f}, {2 * hw, H, 0.2f}, Color{30, 26, 60, 255});
    // blocos
    const Color rows[5] = {Color{239, 71, 111, 255}, Color{255, 159, 28, 255}, Color{255, 209, 102, 255},
                           Color{46, 196, 182, 255}, Color{131, 106, 255, 255}};
    if (rs.prevFlags.size() != g.targets.size()) rs.prevFlags.assign(g.targets.size(), 0);
    for (size_t i = 0; i < g.targets.size(); ++i) {
        const auto &tg = g.targets[i];
        const int row = static_cast<int>((tg.y - 0.35f) / (tg.h / 0.9f));
        Color c = rows[std::clamp(row, 0, 4)];
        if (tg.broken) {
            if (!rs.prevFlags[i]) {
                rs.prevFlags[i] = 1;
                rs.fx.burst({tg.x, tg.y, RG::kTargetZ}, 26, c, 4.5f, 0.12f, 1.1f, 9.f, rs.seed++);
            }
            continue;
        }
        if (tg.hp > 1) c = ColorBrightness(c, -0.35f);
        if (tg.flash > 0) c = mixColor(c, WHITE, tg.flash * 2.5f);
        drawBox({tg.x, tg.y, RG::kTargetZ - 0.1f}, {tg.w, tg.h, 0.3f}, c);
        if (tg.hp > 1) drawBox({tg.x, tg.y, RG::kTargetZ + 0.06f}, {tg.w * 0.8f, 0.05f, 0.02f}, Color{240, 240, 240, 255});
    }
    // lançador
    const float pulse = 0.5f + 0.5f * std::sin(t * 6);
    drawTorus({g.launcherX, g.launcherY, RG::kTargetZ + 1.5f}, 0.35f, {0, 1, 0}, 0, mixColor(Color{120, 255, 230, 255}, WHITE, pulse));
    // bolas e sombras
    for (const auto &b : g.balls) {
        drawSphere(v3(b.pos), RG::kBallR, Color{255, 245, 200, 255});
        drawEllipsoid({b.pos.x, 0.01f, b.pos.z}, {RG::kBallR * 1.2f, 0.005f, RG::kBallR * 1.2f}, Color{20, 15, 40, 255});
    }
    w.end();

    if (g.score() > rs.prevScore) rs.prevScore = g.score();
    BeginMode3D(cam);
    // rastro das bolas (sem luz, translúcido)
    for (const auto &b : g.balls)
        for (int k = 1; k <= 5; ++k) {
            const Vector3 p = Vector3Subtract(v3(b.pos), Vector3Scale(v3(b.vel), 0.02f * k));
            DrawSphereEx(p, RG::kBallR * (1 - k * 0.14f), 6, 8, Color{255, 220, 120, static_cast<unsigned char>(140 - k * 25)});
        }
    rs.fx.draw();
    rlDisableDepthMask();
    for (int i = 0; i < 2; ++i) app.silhouettes.draw3D(i, {0, 0, 0}, 1.f, 0.7f);
    rlEnableDepthMask();
    EndMode3D();
}

} // namespace k3d
