#pragma once
// Aplicativo para PC: espera o Kinect, oferece "Ver sensores" ou "Jogar".
#include <memory>
#include <string>
#include <vector>

#include "raylib.h"

#include "app/audio.h"
#include "app/fileio.h"
#include "app/silhouette.h"
#include "app/ui.h"
#include "body/body_tracker.h"
#include "game/adventure.h"
#include "kinect/body_source.h"
#include "kinect/device_watch.h"
#include "kinect/pc_session.h"
#include "playback_source.h"
#ifdef K3D_HAVE_NITE
#include "kinect/nite_tracker.h"
#endif

namespace k3d {

class App;

class Scene {
public:
    virtual ~Scene() = default;
    virtual void enter(App &) {}
    virtual void update(App &, float dt) = 0;
    virtual void draw(App &) = 0;
    virtual void leave(App &) {}
    virtual const char *name() const = 0;
    // Cenas de jogo controlam o boneco simulado pelo teclado; menus usam o mouse.
    virtual bool wantsKeyboardBody() const { return false; }
    // Cursor de mão (gesto) ativo nesta cena?
    virtual bool wantsHandCursor() const { return true; }
};

enum class InputMode { None, Kinect, Recording, Simulation };

struct Paths {
    std::string data;        // perfil
    std::string documents;   // pasta visível do usuário
    std::string photos, captures, recordings;
};

class App {
public:
    int run(int argc, char **argv);

    // Troca de cena (efetiva no início do próximo quadro)
    void go(std::unique_ptr<Scene> s) { next_ = std::move(s); }
    Scene *scene() { return scene_.get(); }

    // Fontes de entrada
    InputMode mode() const { return mode_; }
    bool openKinect(std::string &err);
    void openSimulation();
    bool openRecording(const std::string &path, std::string &err);
    void closeInput();
    const char *modeLabel() const;
    bool simulated() const { return mode_ == InputMode::Simulation || mode_ == InputMode::Recording; }
    PcKinectSession *kinect() { return kinect_.get(); }
    PlaybackSource *playback() { return playback_.get(); }
    FrameSource *frameSource();         // Kinect ou gravação (nulo na simulação)
    SimFeed *sim() { return simFeed_; }
    bool streamVideoIr(bool ir);        // troca RGB/IR no Kinect ao vivo
    // Foto mais recente da câmera RGB (Kinect por libfreenect, NiTE/OpenNI2 ou gravação)
    bool copyCameraRgb(std::vector<uint8_t> &rgb, int &w, int &h);

    // Rastreamento corporal (Linux): NiTE2 (esqueleto, se o usuário tiver o NiTE) ou o rastreador
    // próprio por silhueta. O NiTE abre o Kinect pelo OpenNI2, então trocar reabre o sensor.
    static bool niteSupported();        // compilado com OpenNI2 + carregador do NiTE
    bool niteFound() const;
    bool usingNite() const;
    bool preferNite = true;             // salvo em config.txt
    std::string niteInfo;               // situação do NiTE para mostrar na tela
    void locateNiteNow();
    bool setPreferNite(bool on, std::string &err);
    // A tela de sensores usa o libfreenect (RGB/IR, gravação, motor): troca enquanto ela está aberta.
    bool useFreenectForSensors(bool on, std::string &err);
    void saveConfig();
    std::string connectedModel;         // nome do modelo quando o vigia do USB não está ativo (Android)

    // Controles de toque da simulação (Android): botões na tela
    struct TouchButton { Rectangle r; const char *label; int id; };
    std::vector<TouchButton> touchPad() const;
    bool touchPadVisible() const;
    void drawTouchPad();

    // Corpo
    const BodyFrame &body() const { return tracker_.frame(); }
    BodyTracker &tracker() { return tracker_; }
    const std::vector<uint16_t> &depthMm() const { return depthMm_; }
    const Intrinsics &intrinsics() const { return intr_; }
    uint64_t depthCount() const { return depthCount_; }
    void setColorSilhouette(bool on) { colorSilhouette_ = on; }
    bool colorSilhouette() const { return colorSilhouette_; }

    // Simulação por teclado (e robô de demonstração)
    void driveSimFromKeyboard(float dt, bool gameplay);
    bool demo = false;              // o robô joga sozinho
    int playersWanted = 1;
    bool touchControls = false;     // --toque: mostra os botões de toque da simulação no PC

    Ui ui;
    Audio audio;
    SilhouetteRenderer silhouettes;
    DeviceWatcher watcher;
    game::Profile profile;
    Paths paths;
    void saveProfile();
    std::string lastKinectError;
    std::string lastHint;

    // Roteiro automático de capturas de tela (verificação)
    std::string shotDir;
    float shotTimer = 0;
    int shotStep = 0;
    bool quit = false;

private:
    void setupPaths();
    void loadConfig();
    bool openKinectFreenect(std::string &err);
    bool openKinectNite(std::string &err);
    bool openKinectAndroid(std::string &err);
    struct SimInput {
        bool left = false, right = false, leanL = false, leanR = false, crouch = false;
        bool jump = false, flap = false, bothHands = false;
        Vector2 pointer{0, 0};
    };
    SimInput readSimInput();
    Vector2 lastPointer_{0, 0};
    bool padPrev_[8] = {};
    void pumpInput(float dt);
    void runScript(float dt);

    std::unique_ptr<Scene> scene_, next_;
    InputMode mode_ = InputMode::None;
    std::unique_ptr<PcKinectSession> kinect_;
#ifdef K3D_HAVE_NITE
    std::unique_ptr<NiteSession> nite_;
    NiteLocation niteLoc_;
#endif
    bool forceFreenect_ = false;        // tela de sensores aberta
    bool niteBroken_ = false;           // NiTE incompatível nesta execução
    bool ignoreUsbWatch_ = false;       // --abrir-kinect (e sempre no Android)
    FrameSource *extSource_ = nullptr;  // Android: fonte aberta pelo Kotlin (não é nossa)
    uint64_t extVersion_ = 0;
public:
    uint64_t deadSourceVersion = ~0ull; // Android: versão da fonte que parou (não reabrir a mesma)
private:
    std::unique_ptr<PlaybackSource> playback_;
    std::unique_ptr<DepthFeed> feed_;
    SimFeed *simFeed_ = nullptr;
    FrameSourceFeed *fsFeed_ = nullptr;
    BodyTracker tracker_;
    std::vector<uint16_t> depthMm_;
    Intrinsics intr_;
    uint64_t depthCount_ = 0;
    bool colorSilhouette_ = true;
    double clock_ = 0;
};

// Fábricas de cenas
std::unique_ptr<Scene> makeWaitScene();
std::unique_ptr<Scene> makeMainMenu();
std::unique_ptr<Scene> makePlayMenu();
std::unique_ptr<Scene> makeAdventureMap();
std::unique_ptr<Scene> makeFreePlay();
std::unique_ptr<Scene> makeGallery();
std::unique_ptr<Scene> makeEmblems();
std::unique_ptr<Scene> makeSensors();
std::unique_ptr<Scene> makeGameScene(game::GameId id, int difficulty, int adventure, uint32_t seed);

} // namespace k3d
