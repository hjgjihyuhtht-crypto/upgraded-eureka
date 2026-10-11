package com.kinect3dstudio.diagnostics

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import com.kinect3dstudio.nativebridge.NativeKinect
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Relatório de fechamento inesperado, para o usuário copiar e mandar.
 * Junta: exceção Java/Kotlin (gravada na hora), falha nativa (sinal + pilha, gravada pelo
 * tratador em crash_handler.cpp), motivo de saída do Android (API 30+) e o log do jogo.
 */
object CrashReport {
    private const val DIR = "relatorios"
    private const val GAME_LOG = "jogo_log.txt"

    private fun dir(c: Context) = File(c.filesDir, DIR).apply { mkdirs() }

    fun processName(): String = runCatching {
        if (Build.VERSION.SDK_INT >= 28) android.app.Application.getProcessName()
        else File("/proc/self/cmdline").readText().trimEnd('\u0000')
    }.getOrDefault("?")

    private fun suffix() = if (processName().endsWith(":game")) "jogo" else "app"

    /** Chamado no início de cada processo (app e jogo). */
    fun install(c: Context) {
        val d = dir(c)
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            runCatching {
                val sw = StringWriter()
                e.printStackTrace(PrintWriter(sw))
                File(d, "erro_java_${suffix()}.txt").writeText(
                    "${stamp()} processo ${processName()}, thread ${t.name}\n$sw",
                )
            }
            previous?.uncaughtException(t, e)
        }
        if (NativeKinect.loadError == null) {
            runCatching { NativeKinect.nativeInstallCrashHandler(File(d, "erro_nativo_${suffix()}.txt").absolutePath) }
        }
    }

    /** Caminho do log do jogo (raylib e etapas de abertura), sobrescrito a cada partida. */
    fun gameLogPath(c: Context): String = File(dir(c), GAME_LOG).absolutePath

    /** Texto para mostrar na tela inicial, ou null se nada fechou de forma inesperada. */
    fun read(c: Context): String? {
        val d = dir(c)
        val parts = mutableListOf<String>()
        d.listFiles { f -> f.name.startsWith("erro_") }?.sortedBy { it.name }?.forEach {
            parts += "== ${it.name} ==\n" + it.readText().take(6000)
        }
        exitReasons(c)?.let { parts += it }
        if (parts.isEmpty()) return null
        File(d, GAME_LOG).takeIf { it.exists() }?.let {
            parts += "== $GAME_LOG (final) ==\n" + it.readLines().takeLast(60).joinToString("\n")
        }
        return parts.joinToString("\n\n")
    }

    fun clear(c: Context) {
        dir(c).listFiles { f -> f.name.startsWith("erro_") }?.forEach { it.delete() }
        c.getSharedPreferences("diag", Context.MODE_PRIVATE).edit()
            .putLong("exitVisto", System.currentTimeMillis()).apply()
    }

    /** Fechamentos por falha registrados pelo Android (Android 11+), mais novos que o último "Apagar". */
    private fun exitReasons(c: Context): String? {
        if (Build.VERSION.SDK_INT < 30) return null
        val am = c.getSystemService(ActivityManager::class.java) ?: return null
        val seen = c.getSharedPreferences("diag", Context.MODE_PRIVATE).getLong("exitVisto", 0L)
        val list = runCatching { am.getHistoricalProcessExitReasons(c.packageName, 0, 6) }.getOrNull() ?: return null
        val crashes = list.filter {
            it.timestamp > seen && (it.reason == ApplicationExitInfo.REASON_CRASH ||
                it.reason == ApplicationExitInfo.REASON_CRASH_NATIVE || it.reason == ApplicationExitInfo.REASON_ANR ||
                it.reason == ApplicationExitInfo.REASON_INITIALIZATION_FAILURE)
        }
        if (crashes.isEmpty()) return null
        return "== Motivos de saída (Android) ==\n" + crashes.joinToString("\n") { e ->
            val kind = when (e.reason) {
                ApplicationExitInfo.REASON_CRASH -> "falha Java/Kotlin"
                ApplicationExitInfo.REASON_CRASH_NATIVE -> "falha nativa (C/C++)"
                ApplicationExitInfo.REASON_ANR -> "travou (ANR)"
                else -> "falha ao iniciar"
            }
            "${stamp(e.timestamp)} ${e.processName}: $kind ${e.description ?: ""}"
        }
    }

    private fun stamp(t: Long = System.currentTimeMillis()) =
        SimpleDateFormat("dd/MM HH:mm:ss", Locale.ROOT).format(Date(t))
}
