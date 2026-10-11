package com.kinect3dstudio.nativebridge

/**
 * Acesso JNI à libk3d_native.so (libusb + libfreenect + sessão).
 * Implementação em app/src/main/cpp/usb_bridge/jni_bridge.cpp.
 */
object NativeKinect {
    /** Erro do carregamento das bibliotecas, ou null se carregaram. */
    val loadError: String? = try {
        System.loadLibrary("usb1.0")
        System.loadLibrary("k3d_native")
        null
    } catch (t: UnsatisfiedLinkError) {
        "Biblioteca nativa não carregou: ${t.message}"
    }

    // Deve espelhar enum StatIndex em kinect_session.h
    const val S_DEPTH_FRAMES = 0
    const val S_VIDEO_FRAMES = 1
    const val S_DEPTH_FPS = 2
    const val S_VIDEO_FPS = 3
    const val S_DEPTH_VALID_RATIO = 4
    const val S_DEPTH_MIN_MM = 5
    const val S_DEPTH_MAX_MM = 6
    const val S_DEPTH_CENTER_MM = 7
    const val S_VIDEO_MEAN = 8
    const val S_SYNC_DELTA_MS = 9
    const val S_DEVICE_DEAD = 10
    const val S_DEPTH_TS = 11
    const val S_VIDEO_TS = 12
    const val S_VIDEO_W = 13
    const val S_VIDEO_H = 14
    const val S_VIDEO_FORMAT = 15
    const val S_REC_ACTIVE = 16
    const val S_REC_FRAMES = 17
    const val S_REC_DROPPED = 18
    const val S_REC_BYTES = 19
    const val S_REC_IO_ERROR = 20
    const val S_PLAY_POSITION = 21
    const val S_PLAY_DURATION_S = 22
    const val S_PLAY_TRUNCATED = 23
    const val S_COUNT = 24

    const val VIDEO_NONE = 0
    const val VIDEO_RGB = 1
    const val VIDEO_IR = 2

    // Valores de freenect_led_options
    const val LED_OFF = 0
    const val LED_GREEN = 1
    const val LED_RED = 2
    const val LED_YELLOW = 3
    const val LED_BLINK_GREEN = 4
    const val LED_BLINK_RED_YELLOW = 6

    // Mapas de cor e filtros (espelham colorize.h)
    const val COLORMAP_GRAY = 0
    const val COLORMAP_TURBO = 1
    const val FILTER_NONE = 0
    const val FILTER_MEDIAN = 1
    const val FILTER_EDGES = 2
    const val FILTER_MEDIAN_EDGES = 3

    external fun nativeVersion(): String
    /** Grava sinal e pilha em [path] se o código nativo cair (diagnóstico). */
    external fun nativeInstallCrashHandler(path: String)
    external fun nativeLastError(): String
    external fun nativeDrainLog(): String
    external fun nativeOpen(cameraFd: Int, motorFd: Int): Long
    external fun nativeDeviceInfo(handle: Long): IntArray
    external fun nativeStartStreams(handle: Long, depth: Boolean, videoMode: Int): Boolean
    external fun nativeStopStreams(handle: Long)
    external fun nativeStats(handle: Long): DoubleArray
    external fun nativeSetLed(handle: Long, mode: Int): Int
    external fun nativeSetTilt(handle: Long, degrees: Double): Int
    external fun nativeReadMotor(handle: Long): DoubleArray?
    external fun nativeClose(handle: Long)

    // Etapa 3 — fontes de quadros (ao vivo ou reprodução de arquivo)
    external fun nativeOpenPlayback(path: String): Long
    external fun nativeIsPlayback(handle: Long): Boolean
    external fun nativePlaybackMetadata(handle: Long): String
    external fun nativeIntrinsicsJson(handle: Long): String
    external fun nativeProbe(handle: Long, x: Int, y: Int): DoubleArray
    external fun nativeCapture(handle: Long, dir: String, filter: Int): String?
    external fun nativeStartRecording(handle: Long, path: String, json: String, depth: Boolean, video: Boolean): Boolean
    external fun nativeStopRecording(handle: Long)
    external fun nativeSetPlayback(handle: Long, speed: Double, paused: Boolean, loop: Boolean)
    external fun nativeSeek(handle: Long, fraction: Double)

    // Renderização em Bitmap ARGB_8888. Retornam a sequência do quadro ou 0 se nada novo.
    // Não são reentrantes por handle: chame de uma única thread por vez.
    external fun nativeRenderDepth(
        handle: Long, bitmap: android.graphics.Bitmap, minMm: Int, maxMm: Int, colormap: Int, filter: Int, afterSeq: Long,
    ): Long
    external fun nativeRenderVideo(handle: Long, bitmap: android.graphics.Bitmap, brightness: Float, contrast: Float, afterSeq: Long): Long
    external fun nativeRenderAligned(handle: Long, bitmap: android.graphics.Bitmap, brightness: Float, contrast: Float, afterSeq: Long): Long
}
