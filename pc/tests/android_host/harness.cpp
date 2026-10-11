// android_mode_test: roda no PC o código do jogo compilado em modo Android (__ANDROID__), com um
// "Kotlin" falso (o que a GameActivity faz): publica uma fonte de quadros sintética (boneco do
// simulador convertido em profundidade bruta do Kinect), espera o jogo usar, revoga como num cabo
// arrancado, confere que o jogo soltou a fonte a tempo e então a destrói. Se o jogo ainda tocasse
// na fonte depois disso, o teste quebraria (rode com -fsanitize=address para pegar uso após free).
// Capturas de tela opcionais: argv[1] = pasta (usa o "import" do ImageMagick, se existir).
#include <jni.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "frame_source.h"
#include "body/sim_depth.h"

int game_main(int, char **);
extern "C" {
JNIEXPORT void JNICALL Java_com_kinect3dstudio_nativebridge_NativeGame_nativeSetWaiting(JNIEnv *, jobject, jstring);
JNIEXPORT void JNICALL Java_com_kinect3dstudio_nativebridge_NativeGame_nativeSetSource(JNIEnv *, jobject, jlong, jstring);
JNIEXPORT jboolean JNICALL Java_com_kinect3dstudio_nativebridge_NativeGame_nativeRevokeSource(JNIEnv *, jobject, jstring, jint);
JNIEXPORT void JNICALL Java_com_kinect3dstudio_nativebridge_NativeGame_nativeSetLogPath(JNIEnv *, jobject, jstring);
}

static const char *JNICALL getUtf(JNIEnv *, jstring s, jboolean *) { return reinterpret_cast<const char *>(s); }
static void JNICALL relUtf(JNIEnv *, jstring, const char *) {}

class FakeKinect : public k3d::FrameSource {
public:
    bool isPlayback() const override { return false; }
    void push(const std::vector<uint16_t> &mm, uint32_t ts) {
        std::vector<uint16_t> raw(mm.size());
        for (size_t i = 0; i < mm.size(); ++i) {
            if (!mm[i]) { raw[i] = 2047; continue; }
            const double z = mm[i] / 1000.0;   // inverso de 1/(raw*-0.0030711016 + 3.3309495161)
            raw[i] = static_cast<uint16_t>(std::lround((1.0 / z - 3.3309495161) / -0.0030711016));
        }
        onDepth(raw.data(), 640, 480, ts);
    }
};

int main(int argc, char **argv) {
    JNINativeInterface_ fn{};
    fn.GetStringUTFChars = getUtf;
    fn.ReleaseStringUTFChars = relUtf;
    JNIEnv_ env{};
    env.functions = &fn;
    auto js = [](const char *s) { return reinterpret_cast<jstring>(const_cast<char *>(s)); };
    const std::string shots = argc > 1 ? argv[1] : "";
    auto shot = [&](const char *name) {
        if (!shots.empty()) (void)!std::system(("import -window root \"" + shots + "/" + name + "\" 2>/dev/null").c_str());
    };

    const char *logp = std::getenv("AND_LOG");
    if (logp) Java_com_kinect3dstudio_nativebridge_NativeGame_nativeSetLogPath(&env, nullptr, js(logp));
    Java_com_kinect3dstudio_nativebridge_NativeGame_nativeSetWaiting(&env, nullptr, js("Procurando o Kinect…"));
    auto *fk = new FakeKinect();
    std::atomic<bool> run{true};
    std::thread producer([&] {
        k3d::SimDepth sim; k3d::SimScene sc; sc.fast = true;
        k3d::SimPose p; p.x = 0.2f;
        uint32_t ts = 0; int frame = 0;
        std::vector<uint16_t> mm;
        while (run) {
            p.handsAuto = (frame / 45) % 2 == 0;
            p.rightHand = {p.x + 0.3f, 1.9f}; p.leftHand = {p.x - 0.3f, 0.8f};
            sim.render(sc, &p, 1, mm);
            fk->push(mm, ts += 1000);
            ++frame;
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    });
    std::thread kotlin([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        shot("android_1_espera.png");
        Java_com_kinect3dstudio_nativebridge_NativeGame_nativeSetSource(&env, nullptr, reinterpret_cast<jlong>(fk), js("Kinect para Xbox 360 (1414)"));
        std::this_thread::sleep_for(std::chrono::milliseconds(3500));
        shot("android_2_menu.png");
        const auto t0 = std::chrono::steady_clock::now();
        const jboolean ok = Java_com_kinect3dstudio_nativebridge_NativeGame_nativeRevokeSource(&env, nullptr, js("O Kinect foi desconectado."), 1500);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("revogar: o jogo soltou a fonte=%d em %.0f ms\n", ok, ms);
        run = false;
        producer.join();
        delete fk;   // o "Kotlin" fecha o Kinect: o jogo não pode mais tocar nele
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        shot("android_3_desconectado.png");
        std::printf(ok ? "android_mode_test: tudo certo\n" : "android_mode_test: FALHOU (o jogo não soltou a fonte)\n");
        std::fflush(stdout);
        _exit(ok ? 0 : 3);
    });
    char a0[] = "raylib";
    char *av[] = {a0, nullptr};
    game_main(1, av);
    kotlin.join();
    return 0;
}
