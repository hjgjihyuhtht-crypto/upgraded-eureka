package com.kinect3dstudio.sensors

import com.kinect3dstudio.device.StreamStats
import com.kinect3dstudio.nativebridge.NativeKinect as N

enum class VideoKind { NONE, RGB, IR }

data class RecordingStats(
    val active: Boolean,
    val framesWritten: Long,
    val framesDropped: Long,
    val bytesWritten: Long,
    val ioError: Boolean,
)

data class PlaybackStats(val position: Double, val durationSeconds: Double, val truncated: Boolean)

/** Estatísticas completas de uma fonte de quadros (ao vivo ou arquivo). Código puro. */
data class SourceStats(
    val stream: StreamStats,
    val videoWidth: Int,
    val videoHeight: Int,
    val videoKind: VideoKind,
    val recording: RecordingStats,
    val playback: PlaybackStats?,
) {
    companion object {
        fun fromArray(s: DoubleArray, isPlayback: Boolean): SourceStats {
            require(s.size >= N.S_COUNT) { "vetor de estatísticas curto: ${s.size}" }
            val stream = StreamStats(
                depthFrames = s[N.S_DEPTH_FRAMES].toLong(),
                videoFrames = s[N.S_VIDEO_FRAMES].toLong(),
                depthFps = s[N.S_DEPTH_FPS],
                videoFps = s[N.S_VIDEO_FPS],
                depthValidRatio = s[N.S_DEPTH_VALID_RATIO],
                depthMinMm = s[N.S_DEPTH_MIN_MM],
                depthMaxMm = s[N.S_DEPTH_MAX_MM],
                depthCenterMm = s[N.S_DEPTH_CENTER_MM],
                videoMeanIntensity = s[N.S_VIDEO_MEAN],
                syncDeltaMs = s[N.S_SYNC_DELTA_MS].takeIf { it >= 0 },
                deviceLost = s[N.S_DEVICE_DEAD] > 0.5,
            )
            return SourceStats(
                stream = stream,
                videoWidth = s[N.S_VIDEO_W].toInt(),
                videoHeight = s[N.S_VIDEO_H].toInt(),
                videoKind = when (s[N.S_VIDEO_FORMAT].toInt()) { 1 -> VideoKind.RGB; 2 -> VideoKind.IR; else -> VideoKind.NONE },
                recording = RecordingStats(
                    active = s[N.S_REC_ACTIVE] > 0.5,
                    framesWritten = s[N.S_REC_FRAMES].toLong(),
                    framesDropped = s[N.S_REC_DROPPED].toLong(),
                    bytesWritten = s[N.S_REC_BYTES].toLong(),
                    ioError = s[N.S_REC_IO_ERROR] > 0.5,
                ),
                playback = if (isPlayback) PlaybackStats(s[N.S_PLAY_POSITION], s[N.S_PLAY_DURATION_S], s[N.S_PLAY_TRUNCATED] > 0.5) else null,
            )
        }
    }
}

/** Resultado da sonda num pixel de profundidade. */
data class DepthProbe(val pixelX: Int, val pixelY: Int, val raw: Int, val mm: Int, val x: Double, val y: Double, val z: Double, val valid: Boolean) {
    companion object {
        fun from(px: Int, py: Int, v: DoubleArray) =
            DepthProbe(px, py, v[1].toInt(), v[2].toInt(), v[3], v[4], v[5], v[0] > 0.5)
    }
}
