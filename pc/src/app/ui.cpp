#include "app/ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "body/body_types.h"

extern "C" {
extern const unsigned char font_Poppins_Bold[];
extern const size_t font_Poppins_Bold_size;
extern const unsigned char font_Poppins_Medium[];
extern const size_t font_Poppins_Medium_size;
}

namespace k3d {

namespace {
constexpr float kHoldSeconds = 1.3f;

std::vector<int> codepoints() {
    std::vector<int> cp;
    for (int c = 32; c < 127; ++c) cp.push_back(c);
    for (int c = 160; c < 256; ++c) cp.push_back(c);
    for (int c : {0x2014, 0x2013, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2212, 0x00D7, 0x00B7})
        cp.push_back(c);
    return cp;
}
} // namespace

void Ui::load() {
    auto cp = codepoints();
    bold_ = LoadFontFromMemory(".ttf", font_Poppins_Bold, static_cast<int>(font_Poppins_Bold_size), 96, cp.data(),
                               static_cast<int>(cp.size()));
    regular_ = LoadFontFromMemory(".ttf", font_Poppins_Medium, static_cast<int>(font_Poppins_Medium_size), 64, cp.data(),
                                  static_cast<int>(cp.size()));
    SetTextureFilter(bold_.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(regular_.texture, TEXTURE_FILTER_BILINEAR);
}

void Ui::unload() {
    UnloadFont(bold_);
    UnloadFont(regular_);
}

void Ui::beginFrame(const BodyFrame &body, bool handCursor, float dt) {
    dt_ = dt;
    scale_ = std::max(0.5f, GetScreenHeight() / 720.f);
    back_ = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_BACK);  // KEY_BACK: botão Voltar do Android
    prevOrder_ = order_;
    order_.clear();
    activate_ = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE);
    navDelta_ = 0;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_TAB)) navDelta_ = 1;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_LEFT)) navDelta_ = -1;
    if (navDelta_ != 0 && !prevOrder_.empty()) {
        auto it = std::find(prevOrder_.begin(), prevOrder_.end(), focus_);
        int idx = it == prevOrder_.end() ? (navDelta_ > 0 ? -1 : 0) : static_cast<int>(it - prevOrder_.begin());
        idx = (idx + navDelta_ + static_cast<int>(prevOrder_.size())) % static_cast<int>(prevOrder_.size());
        focus_ = prevOrder_[idx];
    }
    const Vector2 m = GetMousePosition();
    mouseMoved_ = std::fabs(m.x - lastMouse_.x) + std::fabs(m.y - lastMouse_.y) > 1;
    lastMouse_ = m;

    // Cursor de mão: a mão mais levantada/esticada do primeiro jogador
    handEnabled_ = handCursor;
    handActive_ = false;
    if (handCursor) {
        if (const PlayerBody *p = body.first()) {
            const float H = p->standY > 1.0f ? p->standY : 1.7f;
            const PlayerBody &b = *p;
            const float scoreL = (b.leftHand.y - 0.55f * H) + std::fabs(b.leftHand.x - b.x) * 0.5f;
            const float scoreR = (b.rightHand.y - 0.55f * H) + std::fabs(b.rightHand.x - b.x) * 0.5f;
            const bool right = scoreR >= scoreL;
            const V2 hnd = right ? b.rightHand : b.leftHand;
            const float sc = right ? scoreR : scoreL;
            if (sc > 0.12f) {
                // caixa confortável: 0,9 m de largura x 0,75 m de altura, deslocada para o lado da mão
                const float cx = b.x + (right ? 0.20f : -0.20f);
                const float nx = std::clamp((hnd.x - cx) / 0.9f + 0.5f, 0.f, 1.f);
                const float ny = std::clamp(1.f - (hnd.y - (0.62f * H)) / 0.75f, 0.f, 1.f);
                const Vector2 target{nx * GetScreenWidth(), ny * GetScreenHeight()};
                const float a = std::min(1.f, dt * 12.f);
                handSmooth_.x += (target.x - handSmooth_.x) * a;
                handSmooth_.y += (target.y - handSmooth_.y) * a;
                hand_ = handSmooth_;
                handActive_ = true;
            }
        }
    }
    hovered_.clear();
}

bool Ui::button(const std::string &id, Rectangle r, const std::string &label, BtnStyle st, bool enabled,
                const std::string &sub) {
    if (enabled) order_.push_back(id);
    const Vector2 m = GetMousePosition();
    const bool mouseOver = enabled && CheckCollisionPointRec(m, r);
    const bool handOver = enabled && handActive_ && CheckCollisionPointRec(hand_, r);
    if (mouseOver && mouseMoved_) focus_ = id;
    const bool focused = enabled && focus_ == id;
    bool clicked = false;
    if (mouseOver && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) clicked = true;
    if (focused && activate_) clicked = true;
    if (handOver) {
        hovered_ = id;
        float &t = hover_[id];
        t += dt_;
        hoverProgress_ = t / kHoldSeconds;
        if (t >= kHoldSeconds) { clicked = true; t = -0.6f; }
    } else {
        hover_[id] = 0;
    }

    Color base = st == BtnStyle::Primary ? theme::teal : st == BtnStyle::Danger ? theme::pink
               : st == BtnStyle::Card ? Color{34, 64, 100, 245} : Color{44, 74, 110, 255};
    if (!enabled) base = Color{50, 60, 75, 200};
    const bool hot = mouseOver || handOver || focused;
    Rectangle rr = r;
    if (hot) { rr.x -= px(3); rr.y -= px(3); rr.width += px(6); rr.height += px(6); }
    DrawRectangleRounded({rr.x + px(4), rr.y + px(6), rr.width, rr.height}, 0.25f, 8, Color{0, 0, 0, 90});
    DrawRectangleRounded(rr, 0.25f, 8, hot ? ColorBrightness(base, 0.15f) : base);
    if (hot) DrawRectangleRoundedLinesEx(rr, 0.25f, 8, px(3), theme::yellow);
    // progresso de "segurar" com a mão: barra na base do botão
    if (handOver) {
        const float f = std::clamp(hover_[id] / kHoldSeconds, 0.f, 1.f);
        DrawRectangleRounded({rr.x + px(10), rr.y + rr.height - px(10), (rr.width - px(20)) * f, px(5)}, 1, 4, theme::yellow);
    }
    const Color tc = enabled ? (st == BtnStyle::Primary ? Color{8, 30, 40, 255} : theme::text) : theme::muted;
    const float fs = st == BtnStyle::Card ? px(26) : std::min(px(30), rr.height * 0.42f);
    if (sub.empty()) {
        text(label, rr.x + rr.width / 2, rr.y + rr.height / 2 - fs * 0.62f, fs, tc, Align::Center, true);
    } else {
        text(label, rr.x + rr.width / 2, rr.y + rr.height * 0.30f - fs * 0.5f, fs, tc, Align::Center, true);
        textBox(sub, {rr.x + px(12), rr.y + rr.height * 0.30f + fs * 0.6f, rr.width - px(24), rr.height * 0.6f},
                px(18), st == BtnStyle::Primary ? Color{10, 40, 50, 255} : theme::muted, Align::Center);
    }
    if (clicked) hover_.clear();
    return clicked;
}

void Ui::endFrame() {
    if (focus_.empty() && !order_.empty() && !handActive_) {
        // sem foco: nada
    }
    if (handActive_) {
        const float r = px(26);
        DrawCircleV(hand_, r + px(4), Color{0, 0, 0, 80});
        DrawCircleV(hand_, r, Color{255, 255, 255, 230});
        DrawCircleV(hand_, r * 0.62f, theme::player1);
        if (!hovered_.empty()) {
            const float f = std::clamp(hover_[hovered_] / kHoldSeconds, 0.f, 1.f);
            DrawRing(hand_, r + px(6), r + px(12), -90, -90 + 360 * f, 48, theme::yellow);
        }
    }
}

float Ui::textWidth(const std::string &t, float size, bool bold) {
    return MeasureTextEx(bold ? bold_ : regular_, t.c_str(), size, size * 0.02f).x;
}

void Ui::text(const std::string &t, float x, float y, float size, Color c, Align a, bool bold) {
    Font &f = bold ? bold_ : regular_;
    const float wv = a == Align::Left ? 0 : MeasureTextEx(f, t.c_str(), size, size * 0.02f).x;
    const float xx = a == Align::Left ? x : a == Align::Center ? x - wv / 2 : x - wv;
    DrawTextEx(f, t.c_str(), {std::round(xx), std::round(y)}, size, size * 0.02f, c);
}

void Ui::textShadow(const std::string &t, float x, float y, float size, Color c, Align a, bool bold) {
    text(t, x + size * 0.05f, y + size * 0.06f, size, Color{0, 0, 0, 150}, a, bold);
    text(t, x, y, size, c, a, bold);
}

float Ui::textBox(const std::string &t, Rectangle r, float size, Color c, Align a, bool bold) {
    // quebra por palavras
    std::vector<std::string> lines;
    std::string line, word;
    auto flush = [&]() {
        if (word.empty()) return;
        const std::string cand = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(cand, size, bold) > r.width) { lines.push_back(line); line = word; }
        else line = cand;
        word.clear();
    };
    for (char ch : t) {
        if (ch == ' ') flush();
        else if (ch == '\n') { flush(); lines.push_back(line); line.clear(); }
        else word += ch;
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    float y = r.y;
    const float lh = size * 1.25f;
    for (auto &l : lines) {
        const float x = a == Align::Left ? r.x : a == Align::Center ? r.x + r.width / 2 : r.x + r.width;
        text(l, x, y, size, c, a, bold);
        y += lh;
    }
    return y - r.y;
}

void Ui::panel(Rectangle r, Color c, float round) {
    DrawRectangleRounded({r.x + px(5), r.y + px(8), r.width, r.height}, round, 8, Color{0, 0, 0, 70});
    DrawRectangleRounded(r, round, 8, c);
}

void Ui::background(Color top, Color bottom) {
    DrawRectangleGradientV(0, 0, w(), h(), top, bottom);
    // padrão suave de pontos
    const float t = static_cast<float>(GetTime());
    for (int i = 0; i < 40; ++i) {
        const float x = std::fmod(i * 173.3f + t * (6 + i % 5), static_cast<float>(w() + 40)) - 20;
        const float y = std::fmod(i * 97.7f, static_cast<float>(h()));
        DrawCircleV({x, y}, px(2 + i % 3), Color{255, 255, 255, static_cast<unsigned char>(18 + (i % 4) * 8)});
    }
}

void Ui::medalIcon(int medal, Vector2 c, float r) {
    if (medal <= 0) {
        DrawCircleLinesV(c, r, theme::muted);
        return;
    }
    const Color col = medal == 1 ? theme::bronze : medal == 2 ? theme::silver : medal == 3 ? theme::gold : theme::platinum;
    DrawTriangle({c.x - r * 0.7f, c.y - r * 2.0f}, {c.x - r * 0.1f, c.y - r * 0.6f}, {c.x + r * 0.3f, c.y - r * 2.0f}, theme::pink);
    DrawTriangle({c.x - r * 0.3f, c.y - r * 2.0f}, {c.x + r * 0.1f, c.y - r * 0.6f}, {c.x + r * 0.7f, c.y - r * 2.0f}, theme::teal);
    DrawCircleV(c, r, ColorBrightness(col, -0.25f));
    DrawCircleV(c, r * 0.82f, col);
    DrawCircleV({c.x - r * 0.25f, c.y - r * 0.25f}, r * 0.22f, Color{255, 255, 255, 120});
}

void Ui::badge(const std::string &t, float x, float y, Color bg, Color fg) {
    const float fs = px(18);
    const float wv = textWidth(t, fs, true) + px(20);
    DrawRectangleRounded({x - wv, y, wv, fs + px(10)}, 0.5f, 6, bg);
    text(t, x - wv / 2, y + px(4), fs, fg, Align::Center, true);
}

} // namespace k3d
