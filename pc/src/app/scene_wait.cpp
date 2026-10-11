// Tela inicial: espera o Kinect ser conectado.
#include <cmath>

#include "app/app.h"
#ifdef __ANDROID__
#include "app/android_bridge.h"
#endif

namespace k3d {

namespace {

void drawKinectIcon(Vector2 c, float s, float t, Color glow) {
    // corpo do sensor + base + três "olhos" (projetor IR, RGB, câmera IR)
    const float pulse = 0.5f + 0.5f * std::sin(t * 3);
    for (int k = 0; k < 3; ++k) {
        const float r = s * (1.2f + k * 0.45f + pulse * 0.2f);
        DrawRing(c, r, r + s * 0.04f, 0, 360, 64, Color{glow.r, glow.g, glow.b, static_cast<unsigned char>(90 - k * 25)});
    }
    DrawRectangleRounded({c.x - s, c.y - s * 0.22f, 2 * s, s * 0.44f}, 0.6f, 8, Color{30, 30, 36, 255});
    DrawRectangleRounded({c.x - s * 0.96f, c.y - s * 0.18f, 1.92f * s, s * 0.36f}, 0.6f, 8, Color{52, 52, 60, 255});
    DrawRectangle(static_cast<int>(c.x - s * 0.06f), static_cast<int>(c.y + s * 0.22f), static_cast<int>(s * 0.12f), static_cast<int>(s * 0.3f), Color{40, 40, 46, 255});
    DrawRectangleRounded({c.x - s * 0.5f, c.y + s * 0.5f, s, s * 0.12f}, 0.6f, 6, Color{40, 40, 46, 255});
    DrawCircleV({c.x - s * 0.55f, c.y}, s * 0.09f, Color{10, 10, 14, 255});
    DrawCircleV({c.x + s * 0.1f, c.y}, s * 0.09f, Color{30, 60, 120, 255});
    DrawCircleV({c.x + s * 0.55f, c.y}, s * 0.09f, Color{10, 10, 14, 255});
    DrawCircleV({c.x - s * 0.82f, c.y}, s * 0.035f, Color{80, 255, 120, static_cast<unsigned char>(120 + 120 * pulse)});
}

class WaitScene : public Scene {
public:
    const char *name() const override { return "espera"; }

    void enter(App &app) override {
        app.audio.music(0);
        app.watcher.pollNow();
        app.locateNiteNow();
        recordings_ = listFiles(app.paths.recordings, ".k3drec");
    }

    void update(App &app, float dt) override {
        t_ += dt;
        const KinectUsbStatus st = app.watcher.status();
        // tenta abrir quando aparece uma câmera (ou quando algo muda no USB)
        if (st.state == KinectUsbStatus::State::V1Ready && (st.changeCount != triedAt_ || !tried_)) {
            if (connectDelay_ <= 0) connectDelay_ = 1.2f;   // dá tempo do Windows/Linux terminar de enumerar
            connectDelay_ -= dt;
            if (connectDelay_ <= 0.05f) {
                tried_ = true;
                triedAt_ = st.changeCount;
                connectDelay_ = 0;
                std::string err;
                if (app.openKinect(err)) {
                    app.audio.play(game::Sfx::Go);
                    app.go(makeMainMenu());
                }
            }
        } else if (st.state != KinectUsbStatus::State::V1Ready) {
            tried_ = false;
            connectDelay_ = 0;
        }
        // arquivo .k3drec arrastado para a janela
        if (IsFileDropped()) {
            FilePathList files = LoadDroppedFiles();
            for (unsigned i = 0; i < files.count; ++i) {
                std::string p = files.paths[i];
                if (p.size() > 7 && p.substr(p.size() - 7) == ".k3drec") {
                    std::string err;
                    if (app.openRecording(p, err)) app.go(makeMainMenu());
                    else message_ = "Não foi possível abrir a gravação: " + err;
                }
            }
            UnloadDroppedFiles(files);
        }
    }

    void draw(App &app) override {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.background();
        const KinectUsbStatus st = app.watcher.status();
        Color glow = theme::teal;
        std::string title = "Aguardando o Kinect";
        std::string detail = "Conecte o Kinect v1 (Xbox 360) ao computador. O programa detecta sozinho.";
        switch (st.state) {
            case KinectUsbStatus::State::V1Ready:
                title = tried_ && !app.lastKinectError.empty() ? "Kinect encontrado, mas não abriu" : "Kinect encontrado! Conectando...";
                detail = st.modelName();
                glow = theme::yellow;
                break;
            case KinectUsbStatus::State::V1NoPower: title = "Falta energia na câmera do Kinect"; detail = st.detail; glow = theme::orange; break;
            case KinectUsbStatus::State::V2:
            case KinectUsbStatus::State::Azure: title = st.modelName() + " não é suportado"; detail = st.detail; glow = theme::pink; break;
            case KinectUsbStatus::State::UsbError: title = "Problema no USB"; detail = st.detail; glow = theme::pink; break;
            default: break;
        }
        ui.textShadow("Kinect 3D Studio", ui.w() / 2.f, 30 * s, 54 * s, theme::text, Align::Center);
        drawKinectIcon({ui.w() / 2.f, 190 * s}, 95 * s, t_, glow);
        const int dots = static_cast<int>(t_ * 2) % 4;
        ui.textShadow(title + (st.state == KinectUsbStatus::State::None ? std::string(dots, '.') : ""), ui.w() / 2.f, 290 * s,
                      34 * s, glow, Align::Center);
        ui.textBox(detail, {ui.w() * 0.15f, 340 * s, ui.w() * 0.7f, 60 * s}, 22 * s, theme::text, Align::Center);
        if (tried_ && !app.lastKinectError.empty() && st.state == KinectUsbStatus::State::V1Ready) {
            ui.textBox(app.lastKinectError + ". " + app.lastHint, {ui.w() * 0.12f, 380 * s, ui.w() * 0.76f, 80 * s}, 20 * s, theme::pink, Align::Center);
        }

        // passo a passo
        const Rectangle p{ui.w() * 0.06f, 470 * s, ui.w() * 0.50f, 230 * s};
        ui.panel(p);
        ui.text("Para conectar", p.x + 20 * s, p.y + 14 * s, 24 * s, theme::yellow, Align::Left, true);
        const char *steps[] = {
            "1. Ligue a fonte do Kinect na tomada (12 V).",
            "2. Conecte o USB do Kinect no computador.",
#ifdef _WIN32
            "3. Só na primeira vez: instale o driver com o Zadig",
            "    (veja LEIA-ME.txt na pasta do programa).",
#else
            "3. Só na primeira vez: instale a regra udev",
            "    (veja o LEIA-ME, arquivo 51-kinect.rules).",
#endif
            "4. Fique de 2 a 3 metros do sensor para jogar."};
        float y = p.y + 54 * s;
        for (const char *line : steps) { ui.text(line, p.x + 22 * s, y, 20 * s, theme::text); y += 32 * s; }

        // ações
        const float bx = ui.w() * 0.60f, bw = ui.w() * 0.34f, bh = 62 * s;
        float by = 470 * s;
        if (st.state == KinectUsbStatus::State::V1Ready && tried_ &&
            ui.button("w_retry", {bx, by, bw, bh}, "Tentar de novo")) {
            tried_ = false;
            app.watcher.pollNow();
        }
        if (st.state == KinectUsbStatus::State::V1Ready && tried_) by += bh + 12 * s;
        if (ui.button("w_sim", {bx, by, bw, bh}, "Jogar sem Kinect", BtnStyle::Secondary, true, "")) {
            app.openSimulation();
            app.go(makeMainMenu());
        }
        by += bh + 12 * s;
        if (!recordings_.empty()) {
            if (ui.button("w_rec", {bx, by, bw, bh}, "Reproduzir última gravação", BtnStyle::Secondary)) {
                std::string err;
                if (app.openRecording(recordings_.front(), err)) app.go(makeMainMenu());
                else message_ = err;
            }
            by += bh + 12 * s;
        }
        if (ui.button("w_quit", {bx, by, bw, bh * 0.8f}, "Sair", BtnStyle::Danger)) app.quit = true;
        ui.text("\"Jogar sem Kinect\" usa um boneco simulado (teclado e mouse). Arraste um arquivo .k3drec para reproduzir uma gravação.",
                ui.w() / 2.f, ui.h() - 26 * s, 16 * s, theme::muted, Align::Center);
        if (!message_.empty()) ui.text(message_, ui.w() / 2.f, ui.h() - 50 * s, 18 * s, theme::pink, Align::Center);
        if (App::niteSupported()) {
            const std::string nite = !app.preferNite ? "Rastreamento: silhueta (NiTE2 desligado no menu)"
                                     : app.niteFound() ? "Rastreamento: NiTE2 encontrado, será usado (esqueleto)"
                                                       : "Rastreamento: NiTE2 não instalado, usando o rastreador próprio (veja o LEIA-ME)";
            ui.text(nite, ui.w() / 2.f, ui.h() - 76 * s, 16 * s, app.niteFound() && app.preferNite ? theme::teal : theme::muted, Align::Center);
        }
    }

private:
    float t_ = 0, connectDelay_ = 0;
    bool tried_ = false;
    uint64_t triedAt_ = ~0ull;
    std::vector<std::string> recordings_;
    std::string message_;
};

#ifdef __ANDROID__
// Android: quem abre o Kinect é o app (UsbManager). Esta tela mostra o andamento e as opções.
class AndroidWaitScene : public Scene {
public:
    const char *name() const override { return "espera"; }
    void enter(App &app) override { app.audio.music(0); }

    void update(App &app, float dt) override {
        t_ += dt;
        if (app.ui.backPressed()) { app.quit = true; return; }
        const android::SourceInfo si = android::source();
        if (si.state == android::SourceState::Simulation && !simStarted_) {
            simStarted_ = true;
            app.openSimulation();
            app.go(makeMainMenu());
            return;
        }
        if (si.state == android::SourceState::Ready && si.version != app.deadSourceVersion && si.version != triedVersion_) {
            triedVersion_ = si.version;
            std::string err;
            if (app.openKinect(err)) {
                app.audio.play(game::Sfx::Go);
                app.go(makeMainMenu());
            }
        }
    }

    void draw(App &app) override {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.background();
        const android::SourceInfo si = android::source();
        Color glow = theme::teal;
        std::string title = "Abrindo o Kinect";
        std::string detail = si.message.empty() ? "Pedindo acesso ao USB e iniciando a profundidade e a câmera." : si.message;
        const bool lost = si.state == android::SourceState::Waiting && !app.lastKinectError.empty();
        if (si.state == android::SourceState::Failed) {
            title = "O Kinect não abriu";
            glow = theme::pink;
        } else if (lost) {
            title = "Kinect desconectado";
            detail = "Reconecte o cabo OTG e confira a fonte de 12 V. Ao reconectar, o jogo continua sozinho.";
            glow = theme::orange;
        } else if (si.state == android::SourceState::Ready && si.version == app.deadSourceVersion) {
            title = "O Kinect parou de responder";
            detail = "Confira o cabo OTG e a fonte de 12 V. Ao reconectar, o jogo continua sozinho.";
            glow = theme::orange;
        }
        ui.textShadow("Expedição Ativa", ui.w() / 2.f, 30 * s, 54 * s, theme::text, Align::Center);
        drawKinectIcon({ui.w() / 2.f, 190 * s}, 95 * s, t_, glow);
        const int dots = static_cast<int>(t_ * 2) % 4;
        ui.textShadow(title + (si.state == android::SourceState::Waiting && !lost ? std::string(dots, '.') : ""), ui.w() / 2.f,
                      290 * s, 34 * s, glow, Align::Center);
        ui.textBox(detail, {ui.w() * 0.12f, 340 * s, ui.w() * 0.76f, 90 * s}, 22 * s, theme::text, Align::Center);

        const float bw = 300 * s, bh = 66 * s, gap = 24 * s;
        float x = ui.w() / 2.f - 1.5f * bw - gap, y = ui.h() - 150 * s;
        if (ui.button("aw_retry", {x, y, bw, bh}, "Tentar de novo", BtnStyle::Primary, si.state != android::SourceState::Waiting || lost)) {
            app.lastKinectError.clear();
            android::requestRetry();
        }
        x += bw + gap;
        if (ui.button("aw_sim", {x, y, bw, bh}, "Jogar sem Kinect", BtnStyle::Secondary)) {
            app.openSimulation();
            app.go(makeMainMenu());
        }
        x += bw + gap;
        if (ui.button("aw_back", {x, y, bw, bh}, "Voltar ao app", BtnStyle::Danger)) app.quit = true;
        ui.text("Kinect v1 + adaptador OTG + fonte de 12 V. \"Jogar sem Kinect\" usa um boneco simulado com botões na tela.",
                ui.w() / 2.f, ui.h() - 40 * s, 16 * s, theme::muted, Align::Center);
    }

private:
    float t_ = 0;
    bool simStarted_ = false;
    uint64_t triedVersion_ = ~0ull;
};
#endif

} // namespace

#ifdef __ANDROID__
std::unique_ptr<Scene> makeWaitScene() { return std::make_unique<AndroidWaitScene>(); }
// A tela de sensores do Android é a do app (Kotlin); o jogo volta ao menu.
std::unique_ptr<Scene> makeSensors() { return makeMainMenu(); }
#else
std::unique_ptr<Scene> makeWaitScene() { return std::make_unique<WaitScene>(); }
#endif

} // namespace k3d
