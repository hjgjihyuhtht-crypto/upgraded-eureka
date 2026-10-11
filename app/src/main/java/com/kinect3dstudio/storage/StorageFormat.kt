package com.kinect3dstudio.storage

/** Funções puras de apoio (testadas em StorageFormatTest). */
object StorageFormat {
    const val DEPTH_FRAME_BYTES = 640L * 480 * 2
    const val RGB_FRAME_BYTES = 640L * 480 * 3
    const val IR_FRAME_BYTES = 640L * 488
    const val RECORD_OVERHEAD = 28L
    const val MIN_FREE_TO_START = 600L * 1024 * 1024
    const val MIN_FREE_TO_CONTINUE = 300L * 1024 * 1024

    fun bytesPerSecond(depth: Boolean, video: String?, fps: Double = 30.0): Double {
        var perFrame = 0L
        if (depth) perFrame += DEPTH_FRAME_BYTES + RECORD_OVERHEAD
        when (video) {
            "RGB" -> perFrame += RGB_FRAME_BYTES + RECORD_OVERHEAD
            "IR" -> perFrame += IR_FRAME_BYTES + RECORD_OVERHEAD
        }
        return perFrame * fps
    }

    /** Minutos de gravação que cabem no espaço livre, mantendo a reserva mínima. */
    fun minutesAvailable(freeBytes: Long, bytesPerSecond: Double): Double =
        if (bytesPerSecond <= 0) Double.POSITIVE_INFINITY
        else ((freeBytes - MIN_FREE_TO_CONTINUE).coerceAtLeast(0) / bytesPerSecond) / 60.0

    fun formatBytes(b: Long): String = when {
        b >= 1L shl 30 -> "%.2f GB".format(b / (1024.0 * 1024 * 1024))
        b >= 1L shl 20 -> "%.1f MB".format(b / (1024.0 * 1024))
        b >= 1L shl 10 -> "%.0f KB".format(b / 1024.0)
        else -> "$b B"
    }

    fun formatDuration(seconds: Double): String {
        val s = seconds.toLong().coerceAtLeast(0)
        return "%d:%02d".format(s / 60, s % 60)
    }
}
