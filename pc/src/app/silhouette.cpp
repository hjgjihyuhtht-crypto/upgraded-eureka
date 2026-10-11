#include "app/silhouette.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "app/ui.h"
#include "rlgl.h"

namespace k3d {

namespace {
constexpr int W = PlayerBody::kSilW, H = PlayerBody::kSilH;
}

void SilhouetteRenderer::init() {
    for (int i = 0; i < 2; ++i) {
        img_[i] = GenImageColor(W, H, BLANK);
        tex_[i] = LoadTextureFromImage(img_[i]);
        SetTextureFilter(tex_[i], TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(tex_[i], TEXTURE_WRAP_CLAMP);  // 200x130 não é potência de 2 (OpenGL ES 2)
    }
    ok_ = true;
}

void SilhouetteRenderer::shutdown() {
    if (!ok_) return;
    for (int i = 0; i < 2; ++i) { UnloadTexture(tex_[i]); UnloadImage(img_[i]); }
    ok_ = false;
}

Color SilhouetteRenderer::color(int i) const { return i == 0 ? theme::player1 : theme::player2; }

void SilhouetteRenderer::update(const BodyFrame &f, bool useColor) {
    if (!ok_) return;
    std::vector<uint8_t> glow(W * H);
    for (int i = 0; i < 2; ++i) {
        const PlayerBody &p = f.players[i];
        present_[i] = p.present && !p.silhouette.empty();
        auto *px = static_cast<Color *>(img_[i].data);
        if (!present_[i]) {
            std::memset(px, 0, W * H * sizeof(Color));
            UpdateTexture(tex_[i], px);
            continue;
        }
        const Color base = color(i);
        const bool rgb = useColor && !p.silhouetteRgb.empty();
        // brilho: borrão da máscara (2 passadas de caixa 5x5 separável)
        std::vector<float> a(W * H), b(W * H);
        for (int k = 0; k < W * H; ++k) a[k] = p.silhouette[k] ? 1.f : 0.f;
        for (int pass = 0; pass < 2; ++pass) {
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    float s = 0;
                    for (int d = -2; d <= 2; ++d) s += a[y * W + std::clamp(x + d, 0, W - 1)];
                    b[y * W + x] = s / 5;
                }
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    float s = 0;
                    for (int d = -2; d <= 2; ++d) s += b[std::clamp(y + d, 0, H - 1) * W + x];
                    a[y * W + x] = s / 5;
                }
        }
        for (int y = 0; y < H; ++y) {
            // imagem: linha 0 = topo (y = 2,6 m)
            const int sy = H - 1 - y;
            for (int x = 0; x < W; ++x) {
                const int k = sy * W + x;
                Color &o = px[y * W + x];
                if (p.silhouette[k]) {
                    const bool edge = x == 0 || x == W - 1 || sy == 0 || sy == H - 1 || !p.silhouette[k - 1] ||
                                      !p.silhouette[k + 1] || !p.silhouette[k - W] || !p.silhouette[k + W];
                    if (edge) { o = ColorBrightness(base, 0.45f); o.a = 255; }
                    else if (rgb) {
                        const uint8_t *c = &p.silhouetteRgb[k * 3];
                        if (c[0] | c[1] | c[2]) o = Color{c[0], c[1], c[2], 245};
                        else o = Color{static_cast<unsigned char>(base.r * 0.55f), static_cast<unsigned char>(base.g * 0.55f),
                                       static_cast<unsigned char>(base.b * 0.55f), 220};
                    } else {
                        // preenchimento com leve gradiente vertical
                        const float t = static_cast<float>(sy) / H;
                        o = Color{static_cast<unsigned char>(base.r * (0.55f + 0.3f * t)),
                                  static_cast<unsigned char>(base.g * (0.55f + 0.3f * t)),
                                  static_cast<unsigned char>(base.b * (0.65f + 0.3f * t)), 225};
                    }
                } else {
                    const float g = std::min(1.f, a[k] * 1.8f);
                    o = Color{base.r, base.g, base.b, static_cast<unsigned char>(g * 150)};
                }
            }
        }
        UpdateTexture(tex_[i], px);
    }
}

void SilhouetteRenderer::draw3D(int i, Vector3 o, float scale, float alpha, float extraX) const {
    if (!ok_ || !present_[i]) return;
    const float w = 4.0f * scale, h = 2.6f * scale;
    const float x0 = o.x - 2.0f * scale + extraX;
    rlSetTexture(tex_[i].id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, static_cast<unsigned char>(alpha * 255));
    rlNormal3f(0, 0, 1);
    rlTexCoord2f(0, 1); rlVertex3f(x0, o.y, o.z);
    rlTexCoord2f(1, 1); rlVertex3f(x0 + w, o.y, o.z);
    rlTexCoord2f(1, 0); rlVertex3f(x0 + w, o.y + h, o.z);
    rlTexCoord2f(0, 0); rlVertex3f(x0, o.y + h, o.z);
    rlEnd();
    rlSetTexture(0);
}

void SilhouetteRenderer::draw2D(int i, Rectangle dst, float alpha) const {
    if (!ok_ || !present_[i]) return;
    DrawTexturePro(tex_[i], {0, 0, static_cast<float>(W), static_cast<float>(H)}, dst, {0, 0}, 0,
                   Color{255, 255, 255, static_cast<unsigned char>(alpha * 255)});
}

} // namespace k3d
