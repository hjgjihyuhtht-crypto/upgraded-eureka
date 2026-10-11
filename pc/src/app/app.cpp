#include "app/app.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

#include "app/fileio.h"
#ifdef __ANDROID__
#include "app/android_bridge.h"
#endif

namespace fs = std::filesystem;

extern "C" {
extern const unsigned char app_icon_png[];
extern const size_t app_icon_png_size;
}

namespace k3d {

void App::setupPaths() {
#if defined(__ANDROID__)
    // perfil na pasta interna do app; fotos/capturas na pasta externa do app
    // (Android/data/com.kinect3dstudio/files), que não precisa de permissão
    paths.data = android::internalDir() + "/jogo";
    paths.documents = android::externalDir() + "/Kinect 3D Studio";
    const char sep = '/';
#elif defined(_WIN32)
    const std::string appdata = envUtf8("APPDATA");
    const std::string home = envUtf8("USERPROFILE");
    paths.data = (appdata.empty() ? home : appdata) + "\\Kinect3DStudio";
    paths.documents = documentsFolder() + "\\Kinect 3D Studio";
    const char sep = '\\';
#else
    const std::string home = envUtf8("HOME");
    const std::string xdg = envUtf8("XDG_DATA_HOME");
    paths.data = (xdg.empty() ? home + "/.local/share" : xdg) + "/kinect3dstudio";
    paths.documents = home + "/Kinect 3D Studio";
    const char sep = '/';
#endif
    paths.photos = paths.documents + sep + "Fotos";
    paths.captures = paths.documents + sep + "Capturas";
    paths.recordings = paths.documents + sep + "Gravacoes";
    for (auto *p : {&paths.data, &paths.photos, &paths.captures, &paths.recordings}) makeDirs(*p);
}

void App::saveProfile() {
    profile.save(paths.data + "/perfil.txt");
}

void App::loadConfig() {
    std::vector<unsigned char> raw;
    if (!readFileUtf8(paths.data + "/config.txt", raw)) return;
    const std::string txt(raw.begin(), raw.end());
    if (txt.find("rastreamento=silhueta") != std::string::npos) preferNite = false;
    if (txt.find("rastreamento=nite") != std::string::npos) preferNite = true;
}

void App::saveConfig() {
    const std::string txt = std::string("rastreamento=") + (preferNite ? "nite" : "silhueta") + "\n";
    writeFileUtf8(paths.data + "/config.txt", txt.data(), txt.size());
}

// ------------------------------------------------------------------ NiTE2

bool App::niteSupported() {
#ifdef K3D_HAVE_NITE
    return true;
#else
    return false;
#endif
}

bool App::niteFound() const {
#ifdef K3D_HAVE_NITE
    return niteLoc_.found;
#else
    return false;
#endif
}

bool App::usingNite() const {
#ifdef K3D_HAVE_NITE
    return mode_ == InputMode::Kinect && nite_ != nullptr;
#else
    return false;
#endif
}

void App::locateNiteNow() {
#ifdef K3D_HAVE_NITE
    niteLoc_ = locateNite(paths.data);
    if (niteLoc_.found) niteInfo = "NiTE2 encontrado em " + niteLoc_.dir;
    else niteInfo = niteLoc_.problem;
#endif
}

bool App::setPreferNite(bool on, std::string &err) {
    preferNite = on;
    saveConfig();
    if (mode_ != InputMode::Kinect) return true;
    return openKinect(err);
}

bool App::useFreenectForSensors(bool on, std::string &err) {
    if (forceFreenect_ == on) return true;
    forceFreenect_ = on;
    if (mode_ != InputMode::Kinect) return true;
    // só reabre se o backend realmente muda
    const bool wantNite = !on && preferNite && niteFound() && !niteBroken_;
    if (usingNite() == wantNite) return true;
    return openKinect(err);
}

bool App::copyCameraRgb(std::vector<uint8_t> &rgb, int &w, int &h) {
    if (FrameSource *src = frameSource()) {
        VideoFrame vf;
        if (src->store().copyVideo(vf) && vf.format == VideoFormat::Rgb && vf.width > 0) {
            rgb.assign(vf.data.begin(), vf.data.begin() + static_cast<size_t>(vf.width) * vf.height * 3);
            w = vf.width;
            h = vf.height;
            return true;
        }
        return false;
    }
#ifdef K3D_HAVE_NITE
    if (nite_) return nite_->copyRgb(rgb, w, h);
#endif
    return false;
}

// ------------------------------------------------------------- fontes de entrada

const char *App::modeLabel() const {
    switch (mode_) {
        case InputMode::Kinect: return "Kinect ao vivo";
        case InputMode::Recording: return "SIMULAÇÃO: gravação";
        case InputMode::Simulation: return "SIMULAÇÃO: sem Kinect";
        default: return "sem entrada";
    }
}

FrameSource *App::frameSource() {
    if (mode_ == InputMode::Kinect && extSource_) return extSource_;
    if (mode_ == InputMode::Kinect) return kinect_.get();
    if (mode_ == InputMode::Recording) return playback_.get();
    return nullptr;
}

void App::closeInput() {
    feed_.reset();
#ifdef __ANDROID__
    if (extSource_) android::release();
#endif
    extSource_ = nullptr;   // Android: quem fecha é o Kotlin
    simFeed_ = nullptr;
    fsFeed_ = nullptr;
    if (kinect_) { kinect_->close(); kinect_.reset(); }
#ifdef K3D_HAVE_NITE
    if (nite_) { nite_->close(); nite_.reset(); }
#endif
    if (playback_) { playback_->close(); playback_.reset(); }
    tracker_.reset();
    depthMm_.clear();
    mode_ = InputMode::None;
}

bool App::openKinect(std::string &err) {
    closeInput();
#ifdef __ANDROID__
    return openKinectAndroid(err);
#endif
#ifdef K3D_HAVE_NITE
    if (preferNite && !forceFreenect_ && !niteBroken_) {
        locateNiteNow();
        if (niteLoc_.found) {
            std::string e;
            if (openKinectNite(e)) return true;
            niteInfo = "O NiTE2 não abriu (" + e + "). Usando o rastreador próprio.";
            PcKinectSession::log("[NiTE2] " + niteInfo);
        }
    }
#endif
    return openKinectFreenect(err);
}

bool App::openKinectNite(std::string &err) {
#ifdef K3D_HAVE_NITE
    auto n = std::make_unique<NiteSession>();
    if (!n->open(niteLoc_, err, true)) return false;
    nite_ = std::move(n);
    feed_ = std::make_unique<NiteFeed>(nite_.get());
    mode_ = InputMode::Kinect;
    niteInfo = "NiTE2 " + nite_->version() + " ativo (esqueleto)";
    lastKinectError.clear();
    lastHint.clear();
    return true;
#else
    err = "esta versão não tem suporte ao NiTE2";
    return false;
#endif
}

bool App::openKinectAndroid(std::string &err) {
#ifdef __ANDROID__
    const android::SourceInfo si = android::source();
    if (si.state != android::SourceState::Ready || !si.src) {
        err = si.state == android::SourceState::Failed ? si.message : "O Kinect ainda está sendo aberto";
        lastKinectError = err;
        return false;
    }
    if (!android::acquire(si.version)) { err = "O Kinect mudou durante a abertura"; return false; }
    extSource_ = si.src;
    extVersion_ = si.version;
    auto feed = std::make_unique<FrameSourceFeed>(extSource_, false);
    fsFeed_ = feed.get();
    feed_ = std::move(feed);
    mode_ = InputMode::Kinect;
    connectedModel = si.model;
    lastKinectError.clear();
    lastHint.clear();
    return true;
#else
    err = "só no Android";
    return false;
#endif
}

bool App::openKinectFreenect(std::string &err) {
    const KinectUsbStatus st = watcher.status();
    auto k = std::make_unique<PcKinectSession>();
    if (!k->open(st.model1414, err)) {
        lastKinectError = err;
        lastHint = kinectOpenHint(PcKinectSession::drainLog() + err);
        return false;
    }
    if (!k->startStreams(true, PcVideo::Rgb, err)) {
        lastKinectError = err;
        lastHint = "O Kinect abriu mas não transmitiu. " + kinectOpenHint(PcKinectSession::drainLog());
        k->close();
        return false;
    }
    kinect_ = std::move(k);
    auto feed = std::make_unique<FrameSourceFeed>(kinect_.get(), false);
    fsFeed_ = feed.get();
    feed_ = std::move(feed);
    mode_ = InputMode::Kinect;
    lastKinectError.clear();
    lastHint.clear();
    return true;
}

bool App::streamVideoIr(bool ir) {
    if (mode_ != InputMode::Kinect || !kinect_) return false;   // (Android: o jogo não troca o vídeo)
    std::string err;
    const bool ok = kinect_->startStreams(true, ir ? PcVideo::Ir : PcVideo::Rgb, err);
    if (fsFeed_) fsFeed_->setColorEnabled(!ir);
    return ok;
}

void App::openSimulation() {
    closeInput();
    auto s = std::make_unique<SimFeed>();
    simFeed_ = s.get();
    simFeed_->count = 1;
    feed_ = std::move(s);
    mode_ = InputMode::Simulation;
}

bool App::openRecording(const std::string &path, std::string &err) {
    closeInput();
    auto p = std::make_unique<PlaybackSource>();
    if (!p->open(path, err)) return false;
    p->setLoop(true);
    playback_ = std::move(p);
    auto feed = std::make_unique<FrameSourceFeed>(playback_.get(), true);
    fsFeed_ = feed.get();
    feed_ = std::move(feed);
    mode_ = InputMode::Recording;
    return true;
}

// ------------------------------------------- controles de toque (simulação)

bool App::touchPadVisible() const {
#ifdef __ANDROID__
    const bool platform = true;
#else
    const bool platform = touchControls;
#endif
    return platform && mode_ == InputMode::Simulation && !demo && scene_ && scene_->wantsKeyboardBody();
}

std::vector<App::TouchButton> App::touchPad() const {
    const float s = ui.s();
    const float b = 118 * s, g = 14 * s, m = 22 * s;
    const float H = static_cast<float>(GetScreenHeight()), W = static_cast<float>(GetScreenWidth());
    std::vector<TouchButton> v;
    // esquerda: andar e inclinar
    v.push_back({{m, H - m - b, b, b}, "<<", 0});
    v.push_back({{m + b + g, H - m - b, b, b}, ">>", 1});
    v.push_back({{m, H - m - 2 * b - g, b, b * 0.8f}, "incl. <", 5});
    v.push_back({{m + b + g, H - m - 2 * b - g, b, b * 0.8f}, "incl. >", 6});
    // direita: ações
    v.push_back({{W - m - b, H - m - b, b, b}, "Pular", 2});
    v.push_back({{W - m - 2 * b - g, H - m - b, b, b}, "Abaixar", 3});
    v.push_back({{W - m - b, H - m - 2 * b - g, b, b * 0.8f}, "Bater", 4});
    v.push_back({{W - m - 2 * b - g, H - m - 2 * b - g, b, b * 0.8f}, "2 mãos", 7});
    return v;
}

void App::drawTouchPad() {
    if (!touchPadVisible()) return;
    for (const TouchButton &t : touchPad()) {
        const bool on = padPrev_[t.id];
        DrawRectangleRounded(t.r, 0.3f, 8, on ? Color{255, 209, 102, 150} : Color{10, 20, 34, 120});
        DrawRectangleRoundedLinesEx(t.r, 0.3f, 8, ui.px(2), Color{255, 255, 255, 140});
        ui.text(t.label, t.r.x + t.r.width / 2, t.r.y + t.r.height / 2 - ui.px(13), ui.px(22), WHITE, Align::Center, true);
    }
    ui.text("Toque fora dos botões para mover a mão", GetScreenWidth() / 2.f, GetScreenHeight() - ui.px(30), ui.px(15),
            Color{255, 255, 255, 170}, Align::Center);
}

App::SimInput App::readSimInput() {
    SimInput in;
    const bool pad = touchPadVisible();
#ifndef __ANDROID__
    in.left = IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A);
    in.right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    in.leanL = IsKeyDown(KEY_Q);
    in.leanR = IsKeyDown(KEY_E);
    in.crouch = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S) || IsKeyDown(KEY_C);
    in.jump = IsKeyPressed(KEY_SPACE);
    in.flap = IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    in.bothHands = IsKeyDown(KEY_LEFT_SHIFT) || (!pad && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    if (!pad) { in.pointer = GetMousePosition(); return in; }
#endif
    if (!pad) {
        // menus no Android: o toque funciona como o mouse
        in.pointer = GetMousePosition();
        lastPointer_ = in.pointer;
        return in;
    }
    bool down[8] = {};
    std::vector<Vector2> pts;
    for (int i = 0; i < GetTouchPointCount(); ++i) pts.push_back(GetTouchPosition(i));
    if (pts.empty() && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) pts.push_back(GetMousePosition());
    const auto buttons = touchPad();
    bool pointerSet = false;
    for (const Vector2 &p : pts) {
        bool hit = false;
        for (const TouchButton &b : buttons)
            if (CheckCollisionPointRec(p, b.r)) { down[b.id] = true; hit = true; }
        if (!hit && !pointerSet) { lastPointer_ = p; pointerSet = true; }
    }
    if (lastPointer_.x == 0 && lastPointer_.y == 0) lastPointer_ = {GetScreenWidth() * 0.6f, GetScreenHeight() * 0.45f};
    in.left |= down[0];
    in.right |= down[1];
    in.leanL |= down[5];
    in.leanR |= down[6];
    in.crouch |= down[3];
    in.jump |= down[2] && !padPrev_[2];
    in.flap |= down[4] && !padPrev_[4];
    in.bothHands |= down[7];
    for (int i = 0; i < 8; ++i) padPrev_[i] = down[i];
    in.pointer = lastPointer_;
    return in;
}

// Teclado/mouse (ou toque, no Android) movem o boneco simulado. Em menus, o ponteiro move a mão direita.
void App::driveSimFromKeyboard(float dt, bool gameplay) {
    if (!simFeed_ || demo) return;
    const SimInput in = readSimInput();
    SimPose &p = simFeed_->poses[0];
    p.present = true;
    simFeed_->count = playersWanted;
    const float speed = 1.6f;
    if (in.left) p.x -= speed * dt;
    if (in.right) p.x += speed * dt;
    p.x = std::fmax(-1.3f, std::fmin(1.3f, p.x));
    const float leanTarget = in.leanL ? -0.35f : in.leanR ? 0.35f : 0.f;
    p.lean += (leanTarget - p.lean) * std::fmin(1.f, dt * 8);
    p.crouch += ((in.crouch ? 1.f : 0.f) - p.crouch) * std::fmin(1.f, dt * 10);
    static float jumpT = 0, flapT = 0;
    if (gameplay && in.jump && jumpT <= 0) jumpT = 0.55f;
    if (jumpT > 0) { jumpT -= dt; p.jump = 0.38f * std::sin((1 - std::fmax(0.f, jumpT) / 0.55f) * 3.14159f); }
    else p.jump = 0;
    if (gameplay && in.flap && flapT <= 0) flapT = 0.5f;

    // ponteiro -> mão direita (área de alcance ao redor do ombro)
    const Vector2 m = in.pointer;
    const float nx = m.x / GetScreenWidth(), ny = m.y / GetScreenHeight();
    const float H = p.height;
    if (flapT > 0) {
        flapT -= dt;
        const float t = 1 - std::fmax(0.f, flapT) / 0.5f;
        const float y = t < 0.35f ? 2.05f - (t / 0.35f) * 1.15f : 0.9f + ((t - 0.35f) / 0.65f) * 1.15f;
        p.handsAuto = false;
        p.leftHand = {p.x - 0.7f, y};
        p.rightHand = {p.x + 0.7f, y};
    } else if (!gameplay) {
        // menus: mão segue o mouse pelo mapeamento inverso do cursor de mão
        p.handsAuto = false;
        p.rightHand = {p.x + 0.20f + (nx - 0.5f) * 0.9f, 0.62f * H + (1 - ny) * 0.75f};
        p.leftHand = {p.x - 0.26f, 0.85f};
    } else {
        p.handsAuto = false;
        const V2 target{p.x + (nx - 0.5f) * 2.4f, (1 - ny) * 2.3f};
        p.rightHand = target;
        p.leftHand = in.bothHands ? V2{2 * p.x - target.x, target.y} : V2{p.x - 0.26f, 0.85f};
    }
    // segundo jogador simulado: espelha o primeiro, um pouco à direita
    if (playersWanted == 2) {
        SimPose &q = simFeed_->poses[1];
        q = p;
        q.height = 1.5f;
        q.x = std::fmin(1.4f, p.x + 1.1f);
        q.rightHand = {p.rightHand.x + 1.1f, std::fmin(p.rightHand.y, 1.75f)};
        q.leftHand = {p.leftHand.x + 1.1f, std::fmin(p.leftHand.y, 1.75f)};
        if (p.x > 0.2f) { p.x = 0.2f; }
    }
}

void App::pumpInput(float dt) {
    if (simFeed_) simFeed_->advance(dt);
#ifdef __ANDROID__
    // O Kotlin trocou ou vai fechar a fonte (cabo saiu, app saindo): solta antes de qualquer leitura
    if (extSource_ && android::source().version != extVersion_) {
        lastKinectError = "O Kinect foi desconectado.";
        lastHint = "Reconecte o cabo OTG e confira a fonte de 12 V.";
        closeInput();
        go(makeWaitScene());
        return;
    }
#endif
    // Kinect sumiu do USB (o vigia confirma a cada segundo) ou a sessão acusou falha
    static float missing = 0;
    const bool camGone = mode_ == InputMode::Kinect && !ignoreUsbWatch_ && watcher.status().state != KinectUsbStatus::State::V1Ready &&
                         watcher.status().state != KinectUsbStatus::State::UsbError;
    missing = camGone ? missing + dt : 0;
#ifdef K3D_HAVE_NITE
    if (mode_ == InputMode::Kinect && nite_ && nite_->incompatible() && missing <= 1.5f) {
        // o NiTE instalado não entrega quadros no formato esperado: segue com o rastreador próprio
        niteBroken_ = true;
        std::string err;
        const bool ok = openKinect(err);
        niteInfo = "O NiTE2 instalado não é compatível (quadros em formato inesperado). Usando o rastreador próprio.";
        if (!ok) { closeInput(); go(makeWaitScene()); }
        return;
    }
    const bool niteLost = mode_ == InputMode::Kinect && nite_ && nite_->deviceLost();
#else
    const bool niteLost = false;
#endif
    bool extLost = false;
    if (mode_ == InputMode::Kinect && extSource_) {
        double st[S_COUNT];
        extSource_->readStats(st);
        extLost = st[S_DEVICE_DEAD] > 0;
        if (extLost) deadSourceVersion = extVersion_;
    }
    if (mode_ == InputMode::Kinect && (kinect_ || niteLost || extLost) &&
        ((kinect_ && kinect_->deviceLost()) || niteLost || extLost || missing > 1.5f)) {
        missing = 0;
        // perdeu o Kinect: volta a esperar
        lastKinectError = "O Kinect foi desconectado.";
        lastHint = "Reconecte o cabo USB e confira a fonte de 12 V.";
        closeInput();
        go(makeWaitScene());
        return;
    }
    if (!feed_) return;
    if (feed_->fetch(depthMm_, intr_)) {
        clock_ += 1.0 / 30.0;
        const BodyFrame &f = tracker_.update(depthMm_.data(), intr_, GetTime(), colorSilhouette_ ? feed_->color() : nullptr,
                                             feed_->external());
        const_cast<BodyFrame &>(f).simulated = feed_->simulated();
        silhouettes.update(f, colorSilhouette_);
        ++depthCount_;
    }
}

// ------------------------------------------------- roteiro de capturas de tela
// Usado para verificar visualmente o programa sem intervenção (--shots PASTA).
namespace {
struct ScriptStep { float wait; void (*action)(App &); };
const ScriptStep kScript[] = {
    {1.5f, [](App &) {}},
    {1.0f, [](App &a) { a.go(makePlayMenu()); }},
    {1.0f, [](App &a) { a.go(makeAdventureMap()); }},
    {1.0f, [](App &a) { a.go(makeFreePlay()); }},
    {1.6f, [](App &a) { a.go(makeGameScene(game::GameId::River, 1, 5, game::adventures()[5].seed)); }},
    {8.0f, [](App &) {}},
    {12.0f, [](App &) {}},
    {14.0f, [](App &a) { a.go(makeGameScene(game::GameId::Rebound, 1, 6, 31)); }},
    {12.0f, [](App &a) { a.go(makeGameScene(game::GameId::Dodge, 1, 8, 33)); }},
    {13.0f, [](App &a) { a.go(makeGameScene(game::GameId::Leaks, 1, 7, 32)); }},
    {16.0f, [](App &a) { a.go(makeGameScene(game::GameId::Bubbles, 1, 9, 41)); }},
    {58.0f, [](App &a) { a.go(makeGameScene(game::GameId::River, 0, 0, 11)); }},
    {2.0f, [](App &a) { a.go(makeGallery()); }},
    {1.5f, [](App &a) { a.go(makeEmblems()); }},
    {2.5f, [](App &a) { a.go(makeSensors()); }},
    {11.0f, [](App &a) { a.playersWanted = 2; a.go(makeGameScene(game::GameId::Rebound, 0, -1, 77)); }},
    {2.5f, [](App &a) { a.playersWanted = 1; a.closeInput(); a.demo = false; a.go(makeWaitScene()); }},
};
constexpr int kScriptLen = sizeof(kScript) / sizeof(kScript[0]);
float gScriptT = 0;
} // namespace

void App::runScript(float dt) {
    if (shotTimer < 0) return;            // aguardando a captura deste quadro
    if (shotStep >= kScriptLen) { quit = true; return; }
    gScriptT += dt;
    if (gScriptT >= kScript[shotStep].wait) {
        shotTimer = -1;                   // captura depois de desenhar
        gScriptT = 0;
    }
}

// --------------------------------------------------------------------- laço

int App::run(int argc, char **argv) {
    bool startSim = false, forceOpen = false, startGame = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--sim")) startSim = true;
        // diagnóstico: abre o Kinect já na partida, sem esperar o vigia do USB
        else if (!std::strcmp(argv[i], "--abrir-kinect")) forceOpen = true;
        else if (!std::strcmp(argv[i], "--demo")) { startSim = true; demo = true; }
        else if (!std::strcmp(argv[i], "--toque")) touchControls = true;   // botões de toque também no PC
        else if (!std::strcmp(argv[i], "--jogar")) { startSim = true; startGame = true; }  // simulação direto no jogo
        else if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) { shotDir = argv[++i]; startSim = true; demo = true; }
    }
#ifdef __ANDROID__
    android::startLog();   // antes de tudo: se algo der errado, o app mostra este log
    TraceLog(LOG_INFO, "K3D: pastas %s | %s", android::internalDir().c_str(), android::externalDir().c_str());
#endif
    setupPaths();
    profile.load(paths.data + "/perfil.txt");
    loadConfig();
    locateNiteNow();
    if (!shotDir.empty()) shotDir = absolutePath(shotDir);   // o NiTE troca a pasta atual por um instante

#ifdef __ANDROID__
    // sem MSAA: vários aparelhos não têm configuração EGL com 4 amostras e a janela não abriria
    SetConfigFlags(FLAG_VSYNC_HINT);
#else
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
#endif
    InitWindow(1280, 720, "Kinect 3D Studio");
    if (!IsWindowReady()) {
        TraceLog(LOG_ERROR, "K3D: a janela OpenGL não abriu");
        return 1;
    }
    TraceLog(LOG_INFO, "K3D: janela %dx%d", GetScreenWidth(), GetScreenHeight());
#ifndef __ANDROID__
    SetWindowMinSize(800, 450);
#endif
    SetExitKey(KEY_NULL);
    {
        Image icon = LoadImageFromMemory(".png", app_icon_png, static_cast<int>(app_icon_png_size));
        if (icon.data) { SetWindowIcon(icon); UnloadImage(icon); }
    }
    SetTargetFPS(60);
    ui.load();
    audio.init();
    silhouettes.init();
#ifdef __ANDROID__
    // o Kotlin abre o Kinect (UsbManager); não há vigia de USB pelo libusb aqui
    ignoreUsbWatch_ = true;
#else
    watcher.start(1000);
#endif

    if (startSim) {
        openSimulation();
        go(startGame ? makeGameScene(game::GameId::Dodge, 0, -1, 7) : makeMainMenu());
    }
    else if (forceOpen) {
        std::string err;
        ignoreUsbWatch_ = true;
        if (openKinect(err)) go(makeMainMenu());
        else { lastKinectError = err; go(makeWaitScene()); }
    } else go(makeWaitScene());

    while (!WindowShouldClose() && !quit) {
        const float dt = std::fmin(GetFrameTime(), 0.05f);
        if (next_) {
            if (scene_) scene_->leave(*this);
            scene_ = std::move(next_);
            scene_->enter(*this);
        }
        if (mode_ == InputMode::Simulation) driveSimFromKeyboard(dt, scene_ && scene_->wantsKeyboardBody());
        pumpInput(dt);
        audio.update();
        ui.beginFrame(body(), mode_ != InputMode::Simulation && !demo && scene_->wantsHandCursor(), dt);
        if (!shotDir.empty()) runScript(dt);
        scene_->update(*this, dt);

        BeginDrawing();
        ClearBackground(theme::bg);
        scene_->draw(*this);
        drawTouchPad();
        // aviso permanente de simulação (dados que não vêm do sensor agora)
        if (simulated()) ui.badge(modeLabel(), ui.w() - ui.px(12), ui.px(12), theme::yellow, Color{30, 20, 0, 255});
        ui.endFrame();
        EndDrawing();
        if (!shotDir.empty() && shotTimer < 0) {
            // captura pedida pelo roteiro (após desenhar)
            Image img = LoadImageFromScreen();
            int size = 0;
            unsigned char *png = ExportImageToMemory(img, ".png", &size);
            char name[64];
            std::snprintf(name, sizeof(name), "/%02d_%s.png", shotStep, scene_->name());
            writeFileUtf8(shotDir + name, png, size);
            MemFree(png);
            UnloadImage(img);
            shotTimer = 0;
            ++shotStep;
            if (shotStep < kScriptLen) kScript[shotStep].action(*this);
        }
    }
    if (scene_) scene_->leave(*this);
    saveProfile();
    closeInput();
    watcher.stop();
    silhouettes.shutdown();
    audio.shutdown();
    ui.unload();
    CloseWindow();
    return 0;
}

} // namespace k3d
