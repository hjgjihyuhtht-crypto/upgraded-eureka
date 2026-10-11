// Cena de jogo: roda um minijogo, desenha HUD, pausa, fotos e resultados.
#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/photos.h"
#include "app/render_games.h"
#include "game/autopilot.h"
#include "game/bubbles.h"
#include "game/dodge.h"
#include "game/leaks.h"
#include "game/rebound.h"
#include "game/river.h"
#include "rlgl.h"

namespace k3d {

using namespace game;

namespace {

class GameScene : public Scene {
public:
    GameScene(GameId id, int diff, int adventure, uint32_t seed) : id_(id), diff_(diff), adv_(adventure), seed_(seed) {}
    const char *name() const override { return "jogo"; }
    bool wantsKeyboardBody() const override { return state_ == State::Playing; }
    bool wantsHandCursor() const override { return state_ != State::Playing; }

    void enter(App &app) override {
        g_ = createGame(id_);
        GameConfig cfg;
        cfg.difficulty = diff_;
        cfg.seed = seed_;
        cfg.players = app.playersWanted;
        g_->start(cfg);
        rt_ = LoadRenderTexture(app.ui.w(), app.ui.h());
        state_ = State::Playing;
        rs_ = RenderState{};
        app.audio.music(1 + static_cast<int>(id_));
        if (app.sim()) {
            app.sim()->count = app.playersWanted;
            if (app.playersWanted == 2) { app.sim()->poses[0].x = -0.6f; app.sim()->poses[1].x = 0.6f; }
        }
        bot_.pose[0] = app.sim() ? app.sim()->poses[0] : SimPose{};
    }

    void leave(App &app) override {
        UnloadRenderTexture(rt_);
        for (auto &p : photos_) if (p.thumb.id) UnloadTexture(p.thumb);
        app.audio.music(0);
    }

    void update(App &app, float dt) override {
        rs_.fx.update(dt);
        toastT_ = std::max(0.f, toastT_ - dt);
        shake_ = std::max(0.f, shake_ - dt * 2.5f);
        if (state_ == State::Results) { resultsT_ += dt; return; }
        const BodyFrame &body = app.body();

        if (state_ == State::Paused) {
            if (app.ui.backPressed() || IsKeyPressed(KEY_P)) state_ = State::Playing;
            return;
        }
        // pausa: Esc/P ou as duas mãos acima da cabeça por 1,5 s
        const PlayerBody *p0 = body.first();
        handsUpT_ = (p0 && p0->handsUp && g_->phase() == Phase::Playing && !app.demo) ? handsUpT_ + dt : 0;
        if (app.ui.backPressed() || IsKeyPressed(KEY_P) || handsUpT_ > 1.5f) {
            state_ = State::Paused;
            pauseReason_ = handsUpT_ > 1.5f ? "Pausa pelo gesto (mãos para o alto)" : "Pausa";
            handsUpT_ = 0;
            return;
        }
        // ninguém na frente do sensor
        const bool someone = body.count() > 0;
        absentT_ = someone ? 0 : absentT_ + dt;
        if (!someone && g_->phase() == Phase::Intro) return;   // espera alguém aparecer
        if (absentT_ > 2.5f && g_->phase() == Phase::Playing) {
            state_ = State::Paused;
            pauseReason_ = "Ninguém na frente do sensor. Volte para continuar.";
            absentT_ = 0;
            return;
        }

        // robô de demonstração
        if (app.demo && app.sim()) {
            bot_.update(dt, *g_, body, app.playersWanted);
            for (int i = 0; i < 2; ++i) app.sim()->poses[i] = bot_.pose[i];
            app.sim()->count = app.playersWanted;
        }

        const int steps = std::max(1, static_cast<int>(std::ceil(dt / (1.f / 60.f))));
        const float h = dt / steps;
        for (int k = 0; k < steps; ++k) {
            ev_.clear();
            g_->update(h, body, ev_);
            app.audio.playAll(ev_.sfx);
            if (!ev_.toast.empty()) { toast_ = ev_.toast; toastT_ = ev_.toastSeconds; }
            shake_ = std::max(shake_, ev_.shake);
            for (auto &ph : ev_.photos) if (pendingPhotos_.size() < 3 && photos_.size() < 6) pendingPhotos_.push_back(ph.caption);
        }
        if (g_->done()) finishGame(app);
    }

    void finishGame(App &app) {
        result_ = g_->result();
        newEmblems_ = app.profile.record(result_, adv_);
        auto more = app.profile.recordPhotos(static_cast<int>(photos_.size()));
        newEmblems_.insert(newEmblems_.end(), more.begin(), more.end());
        app.saveProfile();
        state_ = State::Results;
        resultsT_ = 0;
        app.audio.play(Sfx::MedalSfx);
    }

    void drawWorld(App &app) {
        RenderInfo ri;
        ri.time = static_cast<float>(GetTime());
        ri.shake = shake_;
        ri.width = rt_.texture.width;
        ri.height = rt_.texture.height;
        // paredes e quadros são vistos dos dois lados
        rlDisableBackfaceCulling();
        switch (id_) {
            case GameId::River: renderRiver(app, static_cast<RiverGame &>(*g_), ri, rs_); break;
            case GameId::Rebound: renderRebound(app, static_cast<ReboundGame &>(*g_), ri, rs_); break;
            case GameId::Dodge: renderDodge(app, static_cast<DodgeGame &>(*g_), ri, rs_); break;
            case GameId::Leaks: renderLeaks(app, static_cast<LeaksGame &>(*g_), ri, rs_); break;
            case GameId::Bubbles: renderBubbles(app, static_cast<BubblesGame &>(*g_), ri, rs_); break;
        }
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
    }

    void draw(App &app) override {
        Ui &ui = app.ui;
        if (rt_.texture.width != ui.w() || rt_.texture.height != ui.h()) {
            UnloadRenderTexture(rt_);
            rt_ = LoadRenderTexture(ui.w(), ui.h());
        }
        BeginTextureMode(rt_);
        ClearBackground(BLACK);
        drawWorld(app);
        EndTextureMode();
        DrawTextureRec(rt_.texture, {0, 0, static_cast<float>(rt_.texture.width), -static_cast<float>(rt_.texture.height)}, {0, 0}, WHITE);

        // fotos pedidas pelo jogo
        if (!pendingPhotos_.empty()) {
            Image img = LoadImageFromTexture(rt_.texture);
            ImageFlipVertical(&img);
            for (auto &cap : pendingPhotos_) {
                PhotoResult r = savePhoto(app, img, gameName(id_), cap);
                if (r.ok) { photos_.push_back(r); flash_ = 1; app.audio.play(Sfx::Click); }
            }
            UnloadImage(img);
            pendingPhotos_.clear();
        }
        if (flash_ > 0) {
            DrawRectangle(0, 0, ui.w(), ui.h(), Color{255, 255, 255, static_cast<unsigned char>(flash_ * 160)});
            flash_ = std::max(0.f, flash_ - GetFrameTime() * 3);
        }
        drawHud(app);
        if (state_ == State::Paused) drawPause(app);
        if (state_ == State::Results) drawResults(app);
    }

    void drawHud(App &app) {
        Ui &ui = app.ui;
        const float s = ui.s();
        // barra superior
        DrawRectangleGradientV(0, 0, ui.w(), static_cast<int>(70 * s), Color{0, 0, 0, 150}, Color{0, 0, 0, 0});
        ui.textShadow(gameName(id_), 22 * s, 12 * s, 30 * s, theme::text);
        ui.text(std::string(difficultyName(diff_)) + (adv_ >= 0 ? std::string(" · ") + adventures()[adv_].name : ""),
                24 * s, 46 * s, 18 * s, theme::muted);
        const Rectangle bar{ui.w() * 0.32f, 24 * s, ui.w() * 0.36f, 14 * s};
        DrawRectangleRounded(bar, 1, 6, Color{255, 255, 255, 50});
        DrawRectangleRounded({bar.x, bar.y, bar.width * g_->progress(), bar.height}, 1, 6, theme::yellow);
        // pontos
        const float sy = app.simulated() ? 40 * s : 6 * s;
        const std::string sc = std::to_string(g_->score());
        ui.textShadow(sc, ui.w() - 30 * s, sy, 52 * s, theme::yellow, Align::Right);
        ui.text("pontos", ui.w() - 30 * s - ui.textWidth(sc, 52 * s, true) - 8 * s, sy + 24 * s, 18 * s, theme::text, Align::Right);
        // fotos tiradas
        for (size_t i = 0; i < photos_.size(); ++i)
            DrawRectangleRounded({ui.w() - (40 + i * 22) * s, sy + 64 * s, 16 * s, 12 * s}, 0.3f, 4, theme::teal);

        // dica da Esquiva
        if (id_ == GameId::Dodge && g_->phase() == Phase::Playing) {
            const auto &dg = static_cast<const DodgeGame &>(*g_);
            if (const auto *o = dg.nextHazard()) {
                const float tArrive = (-o->z - dg.traveled) / dg.speed;
                if (tArrive < 1.6f && tArrive > -0.1f) {
                    const float a = std::clamp(1.6f - tArrive, 0.f, 1.f);
                    ui.textShadow(dodgeHint(o->kind), ui.w() / 2.f, ui.h() * 0.20f, (56 + 20 * a) * s,
                                  Color{255, 230, 80, static_cast<unsigned char>(160 + 95 * a)}, Align::Center);
                }
            }
        }
        // mensagens
        if (toastT_ > 0 && !toast_.empty())
            ui.textShadow(toast_, ui.w() / 2.f, ui.h() * 0.30f, 48 * s, theme::text, Align::Center);

        // introdução: contagem + como jogar
        if (g_->phase() == Phase::Intro) {
            const bool someone = app.body().count() > 0;
            ui.panel({ui.w() * 0.18f, ui.h() * 0.52f, ui.w() * 0.64f, 200 * s}, Color{12, 24, 40, 220});
            ui.text("Como jogar", ui.w() / 2.f, ui.h() * 0.52f + 16 * s, 26 * s, theme::yellow, Align::Center, true);
            ui.textBox(gameHowTo(id_), {ui.w() * 0.2f, ui.h() * 0.52f + 56 * s, ui.w() * 0.6f, 120 * s}, 24 * s, theme::text, Align::Center);
            if (someone) {
                const int c = static_cast<int>(std::ceil(g_->introLeft()));
                ui.textShadow(std::to_string(std::max(1, c)), ui.w() / 2.f, ui.h() * 0.14f, 120 * s, theme::yellow, Align::Center);
            } else {
                ui.textShadow("Fique de frente para o sensor", ui.w() / 2.f, ui.h() * 0.2f, 44 * s, theme::text, Align::Center);
            }
        }
        // orientações de posição
        if (const PlayerBody *p = app.body().first()) {
            std::string msg;
            if (p->guidance & G_TOO_CLOSE) msg = "Dê um passo para trás";
            else if (p->guidance & G_HEAD_CUT) msg = "Afaste-se: sua cabeça está fora da imagem";
            else if (p->guidance & G_TOO_FAR) msg = "Chegue mais perto do sensor";
            else if (p->guidance & G_FEET_CUT) msg = "Seus pés estão fora da imagem";
            else if (p->guidance & G_LEFT_EDGE) msg = "Vá um pouco para a direita";
            else if (p->guidance & G_RIGHT_EDGE) msg = "Vá um pouco para a esquerda";
            if (!msg.empty()) {
                const float wv = ui.textWidth(msg, 24 * s, true) + 40 * s;
                ui.panel({ui.w() / 2.f - wv / 2, ui.h() - 70 * s, wv, 46 * s}, Color{239, 71, 111, 230}, 0.5f);
                ui.text(msg, ui.w() / 2.f, ui.h() - 62 * s, 24 * s, WHITE, Align::Center, true);
            }
        }
        if (handsUpT_ > 0.2f) {
            ui.text("Mãos para o alto: pausa...", ui.w() / 2.f, ui.h() - 110 * s, 22 * s, theme::yellow, Align::Center, true);
        }
        // mini silhueta (o que o sensor vê)
        // com os botões de toque na tela (Android), a miniatura sobe para baixo do título
        const Rectangle mini = app.touchPadVisible() ? Rectangle{18 * s, 86 * s, 160 * s, 104 * s}
                                                     : Rectangle{18 * s, ui.h() - 150 * s, 160 * s, 104 * s};
        DrawRectangleRounded(mini, 0.15f, 6, Color{0, 0, 0, 110});
        for (int i = 0; i < 2; ++i) app.silhouettes.draw2D(i, mini, 1.f);
    }

    void drawPause(App &app) {
        Ui &ui = app.ui;
        const float s = ui.s();
        DrawRectangle(0, 0, ui.w(), ui.h(), Color{0, 0, 0, 150});
        const Rectangle p{ui.w() / 2.f - 260 * s, ui.h() / 2.f - 200 * s, 520 * s, 400 * s};
        ui.panel(p);
        ui.text("Jogo pausado", p.x + p.width / 2, p.y + 24 * s, 40 * s, theme::text, Align::Center, true);
        ui.text(pauseReason_, p.x + p.width / 2, p.y + 76 * s, 20 * s, theme::muted, Align::Center);
        const float bw = 360 * s, bh = 64 * s, x = p.x + (p.width - bw) / 2;
        if (ui.button("p_cont", {x, p.y + 120 * s, bw, bh}, "Continuar")) state_ = State::Playing;
        if (ui.button("p_restart", {x, p.y + 200 * s, bw, bh}, "Recomeçar", BtnStyle::Secondary)) app.go(makeGameScene(id_, diff_, adv_, seed_));
        if (ui.button("p_exit", {x, p.y + 280 * s, bw, bh}, "Sair do jogo", BtnStyle::Danger))
            app.go(adv_ >= 0 ? makeAdventureMap() : makeFreePlay());
    }

    void drawResults(App &app) {
        Ui &ui = app.ui;
        const float s = ui.s();
        DrawRectangle(0, 0, ui.w(), ui.h(), Color{0, 0, 0, static_cast<unsigned char>(std::min(1.f, resultsT_ * 2) * 170)});
        const Rectangle p{ui.w() / 2.f - 470 * s, 40 * s, 940 * s, ui.h() - 80 * s};
        ui.panel(p);
        ui.text(adv_ >= 0 ? adventures()[adv_].name : gameName(id_), p.x + p.width / 2, p.y + 18 * s, 40 * s, theme::text, Align::Center, true);
        // pontuação subindo
        const int shown = static_cast<int>(result_.score * std::min(1.f, resultsT_ / 1.2f));
        ui.text(std::to_string(shown) + " / " + std::to_string(result_.maxScore) + " pontos", p.x + p.width / 2, p.y + 74 * s,
                34 * s, theme::yellow, Align::Center, true);
        // medalha
        const int medal = resultsT_ > 1.2f ? static_cast<int>(result_.medal) : 0;
        const Vector2 mc{p.x + 150 * s, p.y + 220 * s};
        const float pop = resultsT_ > 1.2f ? 1.f + 0.25f * std::max(0.f, 1.5f - resultsT_) : 1.f;
        ui.medalIcon(medal, mc, 58 * s * pop);
        ui.text(resultsT_ > 1.2f ? medalName(result_.medal) : "...", mc.x, mc.y + 72 * s, 28 * s, theme::text, Align::Center, true);
        // estatísticas
        float y = p.y + 140 * s;
        auto line = [&](const std::string &k, const std::string &v) {
            ui.text(k, p.x + 300 * s, y, 22 * s, theme::muted);
            ui.text(v, p.x + 600 * s, y, 22 * s, theme::text, Align::Left, true);
            y += 32 * s;
        };
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f s", result_.seconds);
        line("Tempo", buf);
        line("Jogadores", std::to_string(result_.playersSeen));
        switch (id_) {
            case GameId::River:
                line("Pedras atingidas", std::to_string(static_cast<int>(result_.stat("rockHits"))));
                line("Anéis", std::to_string(static_cast<int>(result_.stat("rings"))));
                std::snprintf(buf, sizeof(buf), "%.1f m", result_.stat("maxHeight"));
                line("Salto mais alto", buf);
                break;
            case GameId::Rebound:
                line("Rebatidas", std::to_string(static_cast<int>(result_.stat("hits"))));
                line("Mais blocos com uma bola", std::to_string(static_cast<int>(result_.stat("maxTargetsOneBall"))));
                break;
            case GameId::Dodge:
                line("Colisões", std::to_string(static_cast<int>(result_.stat("collisions"))));
                line("Pulos", std::to_string(static_cast<int>(result_.stat("jumps"))));
                break;
            case GameId::Leaks:
                std::snprintf(buf, sizeof(buf), "%.0f%%", result_.stat("maxWaterFrac") * 100);
                line("Água máxima", buf);
                line("Tapados juntos (recorde)", std::to_string(static_cast<int>(result_.stat("maxSimultaneousSeals"))));
                break;
            case GameId::Bubbles:
                std::snprintf(buf, sizeof(buf), "%.1f m", result_.stat("maxFloat"));
                line("Altura máxima", buf);
                line("Bolhas seguidas (2 s)", std::to_string(static_cast<int>(result_.stat("maxCombo"))));
                break;
        }
        // emblemas novos
        if (!newEmblems_.empty() && resultsT_ > 1.6f) {
            y += 8 * s;
            ui.text("Emblemas conquistados!", p.x + 300 * s, y, 24 * s, theme::yellow, Align::Left, true);
            y += 34 * s;
            for (size_t i = 0; i < newEmblems_.size() && i < 3; ++i) {
                ui.text(std::string("• ") + newEmblems_[i]->name + " — " + newEmblems_[i]->description, p.x + 300 * s, y, 19 * s, theme::text);
                y += 28 * s;
            }
        }
        // fotos
        const float ty = p.y + p.height - 230 * s;
        if (!photos_.empty()) {
            ui.text("Fotos desta partida (salvas em Documentos/Kinect 3D Studio/Fotos)", p.x + 30 * s, ty, 18 * s, theme::muted);
            for (size_t i = 0; i < photos_.size() && i < 4; ++i)
                DrawTexturePro(photos_[i].thumb, {0, 0, 320, 180}, {p.x + 30 * s + i * 220 * s, ty + 28 * s, 208 * s, 117 * s}, {0, 0}, 0, WHITE);
        }
        // botões
        const float by = p.y + p.height - 80 * s, bw = 270 * s, bh = 60 * s;
        float bx = p.x + 30 * s;
        if (ui.button("r_again", {bx, by, bw, bh}, "Jogar de novo")) app.go(makeGameScene(id_, diff_, adv_, seed_));
        bx += bw + 20 * s;
        if (adv_ >= 0 && adv_ + 1 < static_cast<int>(adventures().size()) && app.profile.expeditionUnlocked((adv_ + 1) / 3)) {
            const auto &a = adventures()[adv_ + 1];
            if (ui.button("r_next", {bx, by, bw, bh}, "Próxima aventura", BtnStyle::Secondary))
                app.go(makeGameScene(a.game, a.difficulty, adv_ + 1, a.seed));
            bx += bw + 20 * s;
        }
        if (ui.button("r_menu", {bx, by, bw, bh}, "Voltar", BtnStyle::Secondary) || app.ui.backPressed())
            app.go(adv_ >= 0 ? makeAdventureMap() : makeFreePlay());
    }

private:
    enum class State { Playing, Paused, Results };
    GameId id_;
    int diff_, adv_;
    uint32_t seed_;
    std::unique_ptr<MiniGame> g_;
    Events ev_;
    Autopilot bot_;
    RenderTexture2D rt_{};
    RenderState rs_;
    State state_ = State::Playing;
    std::string toast_, pauseReason_;
    float toastT_ = 0, shake_ = 0, handsUpT_ = 0, absentT_ = 0, resultsT_ = 0, flash_ = 0;
    std::vector<std::string> pendingPhotos_;
    std::vector<PhotoResult> photos_;
    Result result_;
    std::vector<const Emblem *> newEmblems_;
};

} // namespace

std::unique_ptr<Scene> makeGameScene(GameId id, int difficulty, int adventure, uint32_t seed) {
    return std::make_unique<GameScene>(id, difficulty, adventure, seed);
}

} // namespace k3d
