// Renderização direta em android.graphics.Bitmap (ARGB_8888 = bytes R,G,B,A).
// Compilado só no Android (precisa de libjnigraphics).
#include <android/bitmap.h>
#include <jni.h>

#include "jni_common.h"

namespace {
struct LockedBitmap {
    JNIEnv *env; jobject bmp; AndroidBitmapInfo info{}; void *px = nullptr; bool ok = false;
    LockedBitmap(JNIEnv *e, jobject b) : env(e), bmp(b) {
        if (AndroidBitmap_getInfo(env, bmp, &info) != ANDROID_BITMAP_RESULT_SUCCESS) return;
        if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) return;
        ok = AndroidBitmap_lockPixels(env, bmp, &px) == ANDROID_BITMAP_RESULT_SUCCESS;
    }
    ~LockedBitmap() { if (ok) AndroidBitmap_unlockPixels(env, bmp); }
};
} // namespace

extern "C" {

// Retornam a sequência do quadro desenhado ou 0 se não houve quadro novo.
JNIEXPORT jlong JNICALL JNI_FN(nativeRenderDepth)(JNIEnv *env, jobject, jlong h, jobject bitmap, jint minMm,
                                                  jint maxMm, jint colormap, jint filter, jlong afterSeq) {
    auto *s = k3d::fromHandle(h);
    if (!s) return 0;
    LockedBitmap b(env, bitmap);
    if (!b.ok) return 0;
    k3d::DepthViewParams p;
    p.minMm = minMm; p.maxMm = maxMm;
    p.colormap = static_cast<k3d::Colormap>(colormap);
    p.filter = static_cast<k3d::DepthFilter>(filter);
    return static_cast<jlong>(s->renderDepth(p, static_cast<uint8_t *>(b.px), b.info.width, b.info.height,
                                             b.info.stride, static_cast<uint64_t>(afterSeq)));
}

JNIEXPORT jlong JNICALL JNI_FN(nativeRenderVideo)(JNIEnv *env, jobject, jlong h, jobject bitmap, jfloat brightness,
                                                  jfloat contrast, jlong afterSeq) {
    auto *s = k3d::fromHandle(h);
    if (!s) return 0;
    LockedBitmap b(env, bitmap);
    if (!b.ok) return 0;
    k3d::VideoViewParams p{brightness, contrast};
    return static_cast<jlong>(s->renderVideo(p, static_cast<uint8_t *>(b.px), b.info.width, b.info.height,
                                             b.info.stride, static_cast<uint64_t>(afterSeq)));
}

JNIEXPORT jlong JNICALL JNI_FN(nativeRenderAligned)(JNIEnv *env, jobject, jlong h, jobject bitmap, jfloat brightness,
                                                    jfloat contrast, jlong afterSeq) {
    auto *s = k3d::fromHandle(h);
    if (!s) return 0;
    LockedBitmap b(env, bitmap);
    if (!b.ok) return 0;
    k3d::VideoViewParams p{brightness, contrast};
    return static_cast<jlong>(s->renderAligned(p, static_cast<uint8_t *>(b.px), b.info.width, b.info.height,
                                               b.info.stride, static_cast<uint64_t>(afterSeq)));
}

} // extern "C"
