// Ponte JNI: com.kinect3dstudio.nativebridge.NativeKinect
// O handle devolvido ao Kotlin é sempre um k3d::FrameSource* (ao vivo ou reprodução).
#include <jni.h>
#include <libusb.h>
#include <string>

#include "jni_common.h"
#include "kinect_session.h"
#include "playback_source.h"

using k3d::FrameSource;
using k3d::KinectSession;
using k3d::PlaybackSource;

thread_local std::string k3d::tLastError;

namespace {
std::string str(JNIEnv *env, jstring s) {
    if (!s) return {};
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string r(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}
KinectSession *live(jlong h) { auto *s = k3d::fromHandle(h); return s ? s->asLive() : nullptr; }
PlaybackSource *play(jlong h) { auto *s = k3d::fromHandle(h); return s ? s->asPlayback() : nullptr; }
jdoubleArray doubles(JNIEnv *env, const double *v, int n) {
    jdoubleArray a = env->NewDoubleArray(n);
    env->SetDoubleArrayRegion(a, 0, n, v);
    return a;
}
} // namespace

extern "C" {

JNIEXPORT jstring JNICALL JNI_FN(nativeVersion)(JNIEnv *env, jobject) {
    const libusb_version *v = libusb_get_version();
    std::string s = "libusb " + std::to_string(v->major) + "." + std::to_string(v->minor) + "." +
                    std::to_string(v->micro) + " + libfreenect (OpenKinect)";
    return env->NewStringUTF(s.c_str());
}

JNIEXPORT jstring JNICALL JNI_FN(nativeLastError)(JNIEnv *env, jobject) {
    return env->NewStringUTF(k3d::tLastError.c_str());
}

JNIEXPORT jstring JNICALL JNI_FN(nativeDrainLog)(JNIEnv *env, jobject) {
    return env->NewStringUTF(KinectSession::drainLog().c_str());
}

// ------------------------------------------------------------------ abertura
JNIEXPORT jlong JNICALL JNI_FN(nativeOpen)(JNIEnv *, jobject, jint cameraFd, jint motorFd) {
    auto *s = new KinectSession();
    if (!s->init(k3d::tLastError) || !s->open(cameraFd, motorFd, k3d::tLastError)) {
        delete s;
        return 0;
    }
    k3d::tLastError.clear();
    return k3d::toHandle(s);
}

JNIEXPORT jlong JNICALL JNI_FN(nativeOpenPlayback)(JNIEnv *env, jobject, jstring path) {
    auto *p = new PlaybackSource();
    if (!p->open(str(env, path), k3d::tLastError)) {
        delete p;
        return 0;
    }
    k3d::tLastError.clear();
    return k3d::toHandle(p);
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeIsPlayback)(JNIEnv *, jobject, jlong h) {
    auto *s = k3d::fromHandle(h);
    return s && s->isPlayback() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL JNI_FN(nativePlaybackMetadata)(JNIEnv *env, jobject, jlong h) {
    auto *p = play(h);
    return env->NewStringUTF(p ? p->metadataJson().c_str() : "");
}

JNIEXPORT void JNICALL JNI_FN(nativeClose)(JNIEnv *, jobject, jlong h) {
    delete k3d::fromHandle(h);  // destrutor virtual: fecha USB ou arquivo
}

// [pidCamera, bcdDevice, is1414, motorAberto]
JNIEXPORT jintArray JNICALL JNI_FN(nativeDeviceInfo)(JNIEnv *env, jobject, jlong h) {
    jintArray arr = env->NewIntArray(4);
    if (auto *s = live(h)) {
        const auto &i = s->info();
        jint v[4] = {i.camera_pid, i.camera_bcd, i.is_model_1414, i.motor_open};
        env->SetIntArrayRegion(arr, 0, 4, v);
    }
    return arr;
}

JNIEXPORT jstring JNICALL JNI_FN(nativeIntrinsicsJson)(JNIEnv *env, jobject, jlong h) {
    auto *s = k3d::fromHandle(h);
    const std::string j = s && s->calibration().valid ? s->calibration().intrinsicsJson() : "null";
    return env->NewStringUTF(j.c_str());
}

// ------------------------------------------------------------------- fluxos
JNIEXPORT jboolean JNICALL JNI_FN(nativeStartStreams)(JNIEnv *, jobject, jlong h, jboolean depth, jint videoMode) {
    auto *s = live(h);
    if (!s) { k3d::tLastError = "Fluxos só podem ser controlados com o Kinect conectado"; return JNI_FALSE; }
    return s->startStreams(depth, static_cast<k3d::VideoMode>(videoMode), k3d::tLastError) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(nativeStopStreams)(JNIEnv *, jobject, jlong h) {
    if (auto *s = live(h)) s->stopStreams();
}

JNIEXPORT jdoubleArray JNICALL JNI_FN(nativeStats)(JNIEnv *env, jobject, jlong h) {
    double buf[k3d::S_COUNT] = {0};
    if (auto *s = k3d::fromHandle(h)) s->readStats(buf);
    return doubles(env, buf, k3d::S_COUNT);
}

// [válido, bruto, mm, X, Y, Z]
JNIEXPORT jdoubleArray JNICALL JNI_FN(nativeProbe)(JNIEnv *env, jobject, jlong h, jint x, jint y) {
    double out[6] = {0};
    if (auto *s = k3d::fromHandle(h)) s->probe(x, y, out);
    return doubles(env, out, 6);
}

// ------------------------------------------------------- captura e gravação
JNIEXPORT jstring JNICALL JNI_FN(nativeCapture)(JNIEnv *env, jobject, jlong h, jstring dir, jint filter) {
    auto *s = k3d::fromHandle(h);
    if (!s) return nullptr;
    const std::string json = s->capture(str(env, dir), static_cast<k3d::DepthFilter>(filter), k3d::tLastError);
    return json.empty() ? nullptr : env->NewStringUTF(json.c_str());
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeStartRecording)(JNIEnv *env, jobject, jlong h, jstring path, jstring json,
                                                        jboolean depth, jboolean video) {
    auto *s = live(h);
    if (!s) { k3d::tLastError = "Gravação disponível apenas com o Kinect ao vivo"; return JNI_FALSE; }
    return s->startRecording(str(env, path), str(env, json), depth, video, k3d::tLastError) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(nativeStopRecording)(JNIEnv *, jobject, jlong h) {
    if (auto *s = k3d::fromHandle(h)) s->stopRecording();
}

// ---------------------------------------------------------------- reprodução
JNIEXPORT void JNICALL JNI_FN(nativeSetPlayback)(JNIEnv *, jobject, jlong h, jdouble speed, jboolean paused, jboolean loop) {
    if (auto *p = play(h)) { p->setSpeed(speed); p->setPaused(paused); p->setLoop(loop); }
}

JNIEXPORT void JNICALL JNI_FN(nativeSeek)(JNIEnv *, jobject, jlong h, jdouble fraction) {
    if (auto *p = play(h)) p->seek(fraction);
}

// --------------------------------------------------------------------- motor
JNIEXPORT jint JNICALL JNI_FN(nativeSetLed)(JNIEnv *, jobject, jlong h, jint mode) {
    auto *s = live(h);
    return s ? s->setLed(mode) : -1;
}

JNIEXPORT jint JNICALL JNI_FN(nativeSetTilt)(JNIEnv *, jobject, jlong h, jdouble deg) {
    auto *s = live(h);
    return s ? s->setTilt(deg) : -1;
}

JNIEXPORT jdoubleArray JNICALL JNI_FN(nativeReadMotor)(JNIEnv *env, jobject, jlong h) {
    auto *s = live(h);
    double buf[5];
    if (!s || !s->readMotor(buf)) return nullptr;
    return doubles(env, buf, 5);
}

} // extern "C"
