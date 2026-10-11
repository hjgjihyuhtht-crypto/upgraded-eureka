package com.kinect3dstudio.diagnostics

import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

enum class LogLevel { INFO, WARN, ERROR, NATIVE }

data class LogEntry(val timeMs: Long, val level: LogLevel, val message: String) {
    fun format(): String {
        val t = SimpleDateFormat("HH:mm:ss.SSS", Locale.ROOT).format(Date(timeMs))
        val tag = when (level) {
            LogLevel.INFO -> "INFO"
            LogLevel.WARN -> "AVISO"
            LogLevel.ERROR -> "ERRO"
            LogLevel.NATIVE -> "NATIVO"
        }
        return "$t [$tag] $message"
    }
}

/** Log de diagnóstico em memória (limitado) e no Logcat. */
class DiagnosticLog(private val max: Int = 500) {
    private val _entries = MutableStateFlow<List<LogEntry>>(emptyList())
    val entries: StateFlow<List<LogEntry>> = _entries

    private fun add(level: LogLevel, msg: String) {
        when (level) {
            LogLevel.ERROR -> Log.e(TAG, msg)
            LogLevel.WARN -> Log.w(TAG, msg)
            else -> Log.i(TAG, msg)
        }
        _entries.update { (it + LogEntry(System.currentTimeMillis(), level, msg)).takeLast(max) }
    }

    fun info(m: String) = add(LogLevel.INFO, m)
    fun warn(m: String) = add(LogLevel.WARN, m)
    fun error(m: String) = add(LogLevel.ERROR, m)
    fun native(m: String) = add(LogLevel.NATIVE, m)
    fun clear() { _entries.value = emptyList() }
    fun export(): String = _entries.value.joinToString("\n") { it.format() }

    private companion object { const val TAG = "K3D" }
}
