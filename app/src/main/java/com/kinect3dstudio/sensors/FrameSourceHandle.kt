package com.kinect3dstudio.sensors

import android.graphics.Bitmap
import com.kinect3dstudio.nativebridge.NativeKinect

/**
 * Referência a uma fonte de quadros nativa: o Kinect ao vivo ou uma gravação.
 * Quem cria é dono do ciclo de vida; as chamadas de render não são reentrantes.
 */
class FrameSourceHandle internal constructor(val handle: Long, val isPlayback: Boolean) {

    fun stats(): SourceStats = SourceStats.fromArray(NativeKinect.nativeStats(handle), isPlayback)

    fun renderDepth(bmp: Bitmap, p: DepthViewParams, afterSeq: Long) =
        NativeKinect.nativeRenderDepth(handle, bmp, p.minMm, p.maxMm, p.colormap, p.filter, afterSeq)

    fun renderVideo(bmp: Bitmap, p: VideoViewParams, afterSeq: Long) =
        NativeKinect.nativeRenderVideo(handle, bmp, p.brightness, p.contrast, afterSeq)

    fun renderAligned(bmp: Bitmap, p: VideoViewParams, afterSeq: Long) =
        NativeKinect.nativeRenderAligned(handle, bmp, p.brightness, p.contrast, afterSeq)

    fun probe(x: Int, y: Int) = DepthProbe.from(x, y, NativeKinect.nativeProbe(handle, x, y))

    /** Grava os quadros atuais em [dir]; devolve JSON de metadados ou null (ver [lastError]). */
    fun capture(dir: String, filter: Int): String? = NativeKinect.nativeCapture(handle, dir, filter)

    fun startRecording(path: String, json: String, depth: Boolean, video: Boolean) =
        NativeKinect.nativeStartRecording(handle, path, json, depth, video)

    fun stopRecording() = NativeKinect.nativeStopRecording(handle)

    fun intrinsicsJson(): String = NativeKinect.nativeIntrinsicsJson(handle)

    fun setPlayback(speed: Double, paused: Boolean, loop: Boolean) =
        NativeKinect.nativeSetPlayback(handle, speed, paused, loop)

    fun seek(fraction: Double) = NativeKinect.nativeSeek(handle, fraction)

    fun playbackMetadata(): String = NativeKinect.nativePlaybackMetadata(handle)

    fun lastError(): String = NativeKinect.nativeLastError()

    companion object {
        /** Abre uma gravação .k3drec. Retorna null em caso de erro (motivo em NativeKinect.nativeLastError()). */
        fun openPlayback(path: String): FrameSourceHandle? {
            if (NativeKinect.loadError != null) return null
            val h = NativeKinect.nativeOpenPlayback(path)
            return if (h == 0L) null else FrameSourceHandle(h, isPlayback = true)
        }
        internal fun live(handle: Long) = FrameSourceHandle(handle, isPlayback = false)
    }

    /** Fecha somente fontes de reprodução; a fonte ao vivo é fechada pelo driver. */
    fun closePlayback() {
        if (isPlayback) NativeKinect.nativeClose(handle)
    }
}

data class DepthViewParams(
    val minMm: Int = 500,
    val maxMm: Int = 4500,
    val colormap: Int = NativeKinect.COLORMAP_TURBO,
    val filter: Int = NativeKinect.FILTER_NONE,
)

data class VideoViewParams(val brightness: Float = 0f, val contrast: Float = 1f)
