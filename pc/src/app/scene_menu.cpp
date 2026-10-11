// Menus: principal (Ver sensores / Jogar), jogar, aventura, jogo livre, fotos e emblemas.
#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/photos.h"

namespace k3d {

using namespace game;

namespace {

Color gameColor(GameId g) {
    switch (g) {
        case GameId::River: return Color{46, 160, 230, 255};
        case GameId::Rebound: return Color{239, 71, 111, 255};
        case GameId::Dodge: return Color{255, 159, 28, 255};
        case GameId::Leaks: return Color{40, 190, 170, 255};
        case GameId::Bubbles: return Color{131, 106, 255, 255};
    }
    return GRAY;
}

void header(App &app, const std::string &title, const std::string &sub = "") {
    Ui &ui = app.ui;
    const float s = ui.s();
    ui.textShadow(title, 40 * s, 24 * s, 46 * s, theme::text);
    if (!sub.empty()) ui.text(sub, 42 * s, 82 * s, 20 * s, theme::muted);
}

// Prévia ao vivo do corpo (o que o sensor vê) num cartão
void bodyPreview(App &app, Rectangle r) {
    Ui &ui = app.ui;
    ui.panel(r, Color{10, 20, 34, 230});
    const Rectangle in{r.x + 6, r.y + 6, r.width - 12, r.height - 12};
    for (int i = 0; i < 2; ++i) app.silhouettes.draw2D(i, in, 1.f);
    const BodyFrame &b = app.body();
    const float s = ui.s();
    // esqueleto do NiTE2 por cima da silhueta (mesma escala: x -2..2 m, y 0..2,6 m)
    static const int bones[][2] = {{0, 1}, {1, 2}, {1, 3}, {2, 4}, {4, 6}, {3, 5}, {5, 7}, {1, 8},
                                   {8, 9}, {8, 10}, {9, 11}, {11, 13}, {10, 12}, {12, 14}};
    for (const PlayerBody &p : b.players) {
        if (!p.present || !p.skeleton) continue;
        auto P = [&](int j) {
            return Vector2{in.x + (p.joints[j].x + 2.f) / 4.f * in.width, in.y + (1.f - p.joints[j].y / 2.6f) * in.height};
        };
        for (auto &bn : bones)
            if (p.jointConf[bn[0]] >= 0.5f && p.jointConf[bn[1]] >= 0.5f) DrawLineEx(P(bn[0]), P(bn[1]), 2.5f * s, theme::yellow);
        for (int j = 0; j < PlayerBody::kJoints; ++j)
            if (p.jointConf[j] >= 0.5f) DrawCircleV(P(j), 3.5f * s, WHITE);
    }
    std::string msg = b.count() == 0 ? "Ninguém na frente do sensor"
                    : b.count() == 1 ? "1 jogador visto" : "2 jogadores vistos";
    ui.text(msg, r.x + r.width / 2, r.y + r.height - 30 * s, 18 * s, theme::text, Align::Center, true);
}

// ------------------------------------------------------------------ principal

class MainMenu : public Scene {
public:
    const char *name() const override { return "menu_principal"; }
    void enter(App &app) override { app.audio.music(0); }
    void update(App &app, float) override {
#ifdef __ANDROID__
        if (app.ui.backPressed()) app.quit = true;   // volta para o app (sensores, capturas)
#else
        if (app.ui.backPressed()) { app.closeInput(); app.go(makeWaitScene()); }
#endif
    }
    void draw(App &app) override {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.background();
        ui.textShadow("Kinect 3D Studio", ui.w() / 2.f, 30 * s, 56 * s, theme::text, Align::Center);
        std::string model = app.watcher.status().modelName();
        if (model.empty()) model = app.connectedModel;
        if (model.empty()) model = "Kinect v1";
        std::string status = app.mode() == InputMode::Kinect
                                 ? "Conectado: " + model + (app.usingNite() ? " (OpenNI2 + NiTE2)" : "")
                                 : app.modeLabel();
        ui.text(status, ui.w() / 2.f, 100 * s, 22 * s, app.mode() == InputMode::Kinect ? theme::teal : theme::yellow, Align::Center, true);
        ui.text("O que você quer fazer?", ui.w() / 2.f, 150 * s, 30 * s, theme::text, Align::Center);

        const float cw = 380 * s, ch = 240 * s, gap = 40 * s;
        const float x0 = ui.w() / 2.f - cw - gap / 2, y0 = 210 * s;
#ifdef __ANDROID__
        // no Android a tela de sensores é a do próprio app
        if (ui.button("m_back", {x0, y0, cw, ch}, "Voltar ao app", BtnStyle::Secondary, true,
                      "Sensores, capturas e gravações ficam nas telas do app Kinect 3D Studio."))
            app.quit = true;
#else
        if (ui.button("m_sensors", {x0, y0, cw, ch}, "Ver sensores", BtnStyle::Secondary, true,
                      "Profundidade, câmera RGB, infravermelho e corpo, ao vivo. Capturas e gravações."))
            app.go(makeSensors());
#endif
        if (ui.button("m_play", {x0 + cw + gap, y0, cw, ch}, "Jogar", BtnStyle::Primary, true,
                      "Expedição Ativa: cinco jogos de movimento, aventura com medalhas, fotos e emblemas."))
            app.go(makePlayMenu());
        bodyPreview(app, {ui.w() / 2.f - 130 * s, y0 + ch + 30 * s, 260 * s, 170 * s});
#ifndef __ANDROID__
        const float by = ui.h() - 80 * s;
        if (ui.button("m_change", {40 * s, by, 260 * s, 56 * s}, "Trocar entrada", BtnStyle::Secondary)) {
            app.closeInput();
            app.go(makeWaitScene());
        }
        if (ui.button("m_quit", {ui.w() - 220 * s, by, 180 * s, 56 * s}, "Sair", BtnStyle::Danger)) app.quit = true;
#endif
        if (App::niteSupported() && app.mode() == InputMode::Kinect) {
            // Rastreamento: NiTE2 (esqueleto) ou próprio (silhueta)
            const std::string label = std::string("Rastreamento: ") + (app.usingNite() ? "NiTE2" : "silhueta");
            if (ui.button("m_track", {ui.w() - 330 * s, 18 * s, 310 * s, 50 * s}, label, BtnStyle::Secondary)) {
                std::string err;
                const bool want = !app.usingNite();
                if (want && !app.niteFound()) app.locateNiteNow();
                if (!want || app.niteFound()) {
                    if (!app.setPreferNite(want, err)) { app.closeInput(); app.go(makeWaitScene()); return; }
                }
            }
            ui.textBox(app.niteInfo, {ui.w() - 330 * s, 74 * s, 310 * s, 60 * s}, 14 * s,
                       app.usingNite() ? theme::teal : theme::muted, Align::Right);
        }
#ifdef __ANDROID__
        if (app.mode() != InputMode::Kinect)
            ui.text("Simulação: toque nos botões. No jogo, botões na tela andam, pulam, abaixam e batem os braços.",
                    ui.w() / 2.f, ui.h() - 20 * s, 16 * s, theme::muted, Align::Center);
        else
            ui.text("Com o Kinect: levante a mão e segure o cursor sobre um botão. O toque na tela também funciona.",
                    ui.w() / 2.f, ui.h() - 20 * s, 16 * s, theme::muted, Align::Center);
#else
        if (app.mode() != InputMode::Kinect)
            ui.text("Simulação: use o mouse nos menus. No jogo: setas andam, Espaço pula, S abaixa, Q/E inclinam, F bate os braços, mouse move a mão.",
                    ui.w() / 2.f, ui.h() - 20 * s, 15 * s, theme::muted, Align::Center);
        else
            ui.text("Com o Kinect: levante a mão e segure o cursor sobre um botão para escolher.", ui.w() / 2.f, ui.h() - 20 * s,
                    16 * s, theme::muted, Align::Center);
#endif
    }
};

// -------------------------------------------------------------------- jogar

class PlayMenu : public Scene {
public:
    const char *name() const override { return "menu_jogar"; }
    void update(App &app, float) override { if (app.ui.backPressed()) app.go(makeMainMenu()); }
    void draw(App &app) override {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.background();
        header(app, "Expedição Ativa", "Jogos originais de movimento com o corpo inteiro.");
        const int medals = app.profile.medalsCount(Medal::Bronze);
        const int emb = static_cast<int>(app.profile.emblemsEarned.size());
        const float cw = 360 * s, ch = 150 * s, gx = 30 * s, gy = 26 * s;
        const float x0 = ui.w() / 2.f - cw - gx / 2, y0 = 130 * s;
        if (ui.button("pm_adv", {x0, y0, cw, ch}, "Aventura", BtnStyle::Primary, true,
                      std::to_string(medals) + " de 15 aventuras com medalha"))
            app.go(makeAdventureMap());
        if (ui.button("pm_free", {x0 + cw + gx, y0, cw, ch}, "Jogo livre", BtnStyle::Card, true, "Escolha o jogo e a dificuldade"))
            app.go(makeFreePlay());
        if (ui.button("pm_photos", {x0, y0 + ch + gy, cw, ch}, "Fotos", BtnStyle::Card, true, "Momentos das partidas"))
            app.go(makeGallery());
        if (ui.button("pm_emb", {x0 + cw + gx, y0 + ch + gy, cw, ch}, "Emblemas", BtnStyle::Card, true,
                      std::to_string(emb) + " de " + std::to_string(emblems().size()) + " conquistados"))
            app.go(makeEmblems());
        const float y2 = y0 + 2 * (ch + gy);
        if (ui.button("pm_players", {x0, y2, cw, 70 * s}, app.playersWanted == 1 ? "Jogadores: 1" : "Jogadores: 2", BtnStyle::Secondary))
            app.playersWanted = app.playersWanted == 1 ? 2 : 1;
        if (ui.button("pm_color", {x0 + cw + gx, y2, cw, 70 * s}, app.colorSilhouette() ? "Você na tela: cores reais" : "Você na tela: silhueta",
                      BtnStyle::Secondary))
            app.setColorSilhouette(!app.colorSilhouette());
        if (ui.button("pm_back", {40 * s, ui.h() - 80 * s, 200 * s, 56 * s}, "Voltar", BtnStyle::Secondary)) app.go(makeMainMenu());
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Partidas: %d   ·   Pontos de aventura: %d   ·   Tempo de jogo: %d min",
                      app.profile.gamesPlayed, app.profile.totalPoints, static_cast<int>(app.profile.secondsPlayed / 60));
        ui.text(buf, ui.w() / 2.f, ui.h() - 64 * s, 18 * s, theme::muted, Align::Center);
        if (app.playersWanted == 2)
            ui.text("Dois jogadores: fiquem lado a lado, a uns 2,5 m do sensor.", ui.w() / 2.f, ui.h() - 36 * s, 18 * s, theme::yellow, Align::Center);
    }
};

// ------------------------------------------------------------------ aventura

class AdventureMap : public Scene {
public:
    const char *name() const override { return "aventura"; }
    void update(App &app, float) override { if (app.ui.backPressed()) app.go(makePlayMenu()); }
    void draw(App &app) override {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.background();
        header(app, "Aventura", "Ganhe medalha nas três aventuras de uma expedição para abrir a próxima.");
        const float top = 118 * s, rowH = (ui.h() - top - 100 * s) / 5.f;
        const float cardW = (ui.w() - 360 * s) / 3.f;
        for (int e = 0; e < 5; ++e) {
            const auto &ex = expeditions()[e];
            const bool open = app.profile.expeditionUnlocked(e);
            const float y = top + e * rowH;
            ui.textBox(ex.name, {30 * s, y + 8 * s, 260 * s, rowH}, 20 * s, open ? theme::text : theme::muted, Align::Left, true);
            for (int k = 0; k < 3; ++k) {
                const int ai = ex.adventures[k];
                const Adventure &a = adventures()[ai];
                const Rectangle r{310 * s + k * (cardW + 14 * s), y + 4 * s, cardW, rowH - 10 * s};
                const std::string sub = std::string(gameName(a.game)) + " · " + difficultyName(a.difficulty) +
                                        (app.profile.bestScore[ai] > 0 ? " · recorde " + std::to_string(app.profile.bestScore[ai]) : "");
                if (ui.button("adv" + std::to_string(ai), r, a.name, BtnStyle::Card, open, open ? sub : "Bloqueada"))
                    app.go(makeGameScene(a.game, a.difficulty, ai, a.seed));
                DrawRectangle(static_cast<int>(r.x), static_cast<int>(r.y + 6 * s), static_cast<int>(6 * s), static_cast<int>(r.height - 12 * s), gameColor(a.game));
                if (open) ui.medalIcon(app.profile.bestMedal[ai], {r.x + r.width - 30 * s, r.y + r.height - 26 * s}, 14 * s);
            }
        }
        if (ui.button("adv_back", {40 * s, ui.h() - 76 * s, 200 * s, 56 * s}, "Voltar", BtnStyle::Secondary)) app.go(makePlayMenu());
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Medalhas: %d bronze+ · %d ouro+ · %d platina", app.profile.medalsCount(Medal::Bronze),
                      app.profile.medalsCount(Medal::Gold), app.profile.medalsCount(Medal::Platinum));
        ui.text(buf, ui.w() - 40 * s, ui.h() - 60 * s, 20 * s, theme::muted, Align::Right);
    }
};

// ---------------------------------------------------------------- jogo livre

class FreePlay : public Scene {
public:
    const char *name() const override { return "jogo_livre"; }
    void update(App &app, float) override { if (app.ui.backPressed()) app.go(makePlayMenu()); }
    void draw(App &app) override {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.background();
        header(app, "Jogo livre", "Escolha um jogo e a dificuldade. Recordes ficam salvos.");
        const float top = 120 * s, rowH = (ui.h() - top - 100 * s) / 5.f;
        for (int gi = 0; gi < kGameCount; ++gi) {
            const GameId id = static_cast<GameId>(gi);
            const float y = top + gi * rowH;
            DrawRectangleRounded({30 * s, y + 6 * s, 10 * s, rowH - 16 * s}, 1, 4, gameColor(id));
            ui.text(gameName(id), 52 * s, y + 6 * s, 28 * s, theme::text, Align::Left, true);
            ui.textBox(gameHowTo(id), {52 * s, y + 42 * s, ui.w() * 0.42f, rowH}, 15 * s, theme::muted);
            for (int d = 0; d < 3; ++d) {
                const float bw = (ui.w() * 0.46f) / 3.f;
                const Rectangle r{ui.w() * 0.50f + d * (bw + 10 * s), y + 8 * s, bw, rowH - 18 * s};
                const int best = app.profile.freeBest[gi][d];
                if (ui.button("fp" + std::to_string(gi * 3 + d), r, difficultyName(d), d == 0 ? BtnStyle::Primary : BtnStyle::Card,
                              true, best > 0 ? "recorde " + std::to_string(best) : ""))
                    app.go(makeGameScene(id, d, -1, 1000u + static_cast<uint32_t>(GetRandomValue(1, 99999))));
            }
        }
        if (ui.button("fp_back", {40 * s, ui.h() - 76 * s, 200 * s, 56 * s}, "Voltar", BtnStyle::Secondary)) app.go(makePlayMenu());
    }
};

// ---------------------------------------------------------------------- fotos

class Gallery : public Scene {
public:
    const char *name() const override { return "fotos"; }
    void enter(App &app) override { reload(app); }
    void leave(App &) override { clear(); }
    void clear() {
        for (auto &t : thumbs_) if (t.id) UnloadTexture(t);
        thumbs_.clear();
        if (big_.id) UnloadTexture(big_);
        big_ = Texture2D{};
    }
    void reload(App &app) {
        clear();
        files_ = listFiles(app.paths.photos, ".png");
        page_ = std::min(page_, std::max(0, (static_cast<int>(files_.size()) - 1) / kPer));
        for (int i = page_ * kPer; i < std::min(static_cast<int>(files_.size()), (page_ + 1) * kPer); ++i)
            thumbs_.push_back(loadTextureUtf8(files_[i], 320, 180));
        selected_ = -1;
    }
    void update(App &app, float) override {
        if (app.ui.backPressed()) {
            if (selected_ >= 0) { selected_ = -1; confirm_ = false; }
            else app.go(makePlayMenu());
        }
    }
    void draw(App &app) override {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.background();
        if (selected_ >= 0) {
            header(app, "Foto", files_[selected_].substr(files_[selected_].find_last_of("/\\") + 1));
            if (big_.id) {
                const float k = std::min((ui.w() - 80 * s) / big_.width, (ui.h() - 230 * s) / big_.height);
                DrawTextureEx(big_, {ui.w() / 2.f - big_.width * k / 2, 120 * s}, 0, k, WHITE);
            }
            const float by = ui.h() - 86 * s;
            if (ui.button("g_back", {40 * s, by, 220 * s, 60 * s}, "Voltar", BtnStyle::Secondary)) { selected_ = -1; confirm_ = false; }
            if (!confirm_) {
                if (ui.button("g_del", {ui.w() - 300 * s, by, 260 * s, 60 * s}, "Excluir foto", BtnStyle::Danger)) confirm_ = true;
            } else {
                ui.text("Excluir definitivamente?", ui.w() - 560 * s, by + 16 * s, 22 * s, theme::pink, Align::Left, true);
                if (ui.button("g_del2", {ui.w() - 300 * s, by, 260 * s, 60 * s}, "Sim, excluir", BtnStyle::Danger)) {
                    removeFileUtf8(files_[selected_]);
                    confirm_ = false;
                    reload(app);
                }
            }
            return;
        }
        header(app, "Fotos", files_.empty() ? "Nenhuma foto ainda: elas são tiradas sozinhas nos melhores momentos das partidas."
                                            : std::to_string(files_.size()) + " fotos em " + app.paths.photos);
        const int cols = 4;
        const float cw = (ui.w() - 80 * s - (cols - 1) * 16 * s) / cols, ch = cw * 9 / 16;
        for (size_t i = 0; i < thumbs_.size(); ++i) {
            const int r = static_cast<int>(i) / cols, c = static_cast<int>(i) % cols;
            const Rectangle rect{40 * s + c * (cw + 16 * s), 120 * s + r * (ch + 16 * s), cw, ch};
            if (ui.button("ph" + std::to_string(i), rect, "", BtnStyle::Card)) {
                selected_ = page_ * kPer + static_cast<int>(i);
                if (big_.id) UnloadTexture(big_);
                big_ = loadTextureUtf8(files_[selected_]);
            }
            if (thumbs_[i].id)
                DrawTexturePro(thumbs_[i], {0, 0, static_cast<float>(thumbs_[i].width), static_cast<float>(thumbs_[i].height)},
                               {rect.x + 4, rect.y + 4, rect.width - 8, rect.height - 8}, {0, 0}, 0, WHITE);
        }
        const float by = ui.h() - 80 * s;
        if (ui.button("g_back2", {40 * s, by, 200 * s, 56 * s}, "Voltar", BtnStyle::Secondary)) app.go(makePlayMenu());
#ifndef __ANDROID__
        if (ui.button("g_open", {260 * s, by, 260 * s, 56 * s}, "Abrir a pasta", BtnStyle::Secondary)) openFolder(app.paths.photos);
#endif
        const int pages = std::max(1, (static_cast<int>(files_.size()) + kPer - 1) / kPer);
        if (pages > 1) {
            if (ui.button("g_prev", {ui.w() - 420 * s, by, 180 * s, 56 * s}, "Anteriores", BtnStyle::Secondary, page_ > 0)) { --page_; reload(app); }
            if (ui.button("g_next", {ui.w() - 220 * s, by, 180 * s, 56 * s}, "Próximas", BtnStyle::Secondary, page_ + 1 < pages)) { ++page_; reload(app); }
        }
    }

private:
    static constexpr int kPer = 12;
    std::vector<std::string> files_;
    std::vector<Texture2D> thumbs_;
    Texture2D big_{};
    int page_ = 0, selected_ = -1;
    bool confirm_ = false;
};

// ------------------------------------------------------------------- emblemas

class Emblems : public Scene {
public:
    const char *name() const override { return "emblemas"; }
    void update(App &app, float) override { if (app.ui.backPressed()) app.go(makePlayMenu()); }
    void draw(App &app) override {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.background();
        header(app, "Emblemas", std::to_string(app.profile.emblemsEarned.size()) + " de " + std::to_string(emblems().size()) + " conquistados");
        const int cols = 4;
        const float cw = (ui.w() - 80 * s - (cols - 1) * 14 * s) / cols, ch = (ui.h() - 230 * s) / 5.f;
        for (size_t i = 0; i < emblems().size(); ++i) {
            const Emblem &e = emblems()[i];
            const bool got = app.profile.hasEmblem(e.id);
            const int r = static_cast<int>(i) / cols, c = static_cast<int>(i) % cols;
            const Rectangle rect{40 * s + c * (cw + 14 * s), 110 * s + r * (ch + 10 * s), cw, ch};
            ui.panel(rect, got ? Color{40, 90, 110, 240} : Color{30, 40, 56, 220});
            const Vector2 ic{rect.x + 34 * s, rect.y + ch / 2};
            DrawPoly(ic, 6, 22 * s, 0, got ? theme::yellow : Color{70, 80, 96, 255});
            DrawPoly(ic, 6, 14 * s, 30, got ? theme::orange : Color{50, 60, 76, 255});
            ui.text(e.name, rect.x + 66 * s, rect.y + 8 * s, 20 * s, got ? theme::text : theme::muted, Align::Left, true);
            ui.textBox(e.description, {rect.x + 66 * s, rect.y + 36 * s, rect.width - 76 * s, ch - 40 * s}, 15 * s, got ? theme::text : theme::muted);
        }
        if (ui.button("e_back", {40 * s, ui.h() - 76 * s, 200 * s, 56 * s}, "Voltar", BtnStyle::Secondary)) app.go(makePlayMenu());
    }
};

} // namespace

std::unique_ptr<Scene> makeMainMenu() { return std::make_unique<MainMenu>(); }
std::unique_ptr<Scene> makePlayMenu() { return std::make_unique<PlayMenu>(); }
std::unique_ptr<Scene> makeAdventureMap() { return std::make_unique<AdventureMap>(); }
std::unique_ptr<Scene> makeFreePlay() { return std::make_unique<FreePlay>(); }
std::unique_ptr<Scene> makeGallery() { return std::make_unique<Gallery>(); }
std::unique_ptr<Scene> makeEmblems() { return std::make_unique<Emblems>(); }

} // namespace k3d
