package com.kinect3dstudio

import android.app.Application
import com.kinect3dstudio.device.KinectV1Device
import com.kinect3dstudio.diagnostics.DiagnosticLog
import com.kinect3dstudio.storage.CaptureStore
import com.kinect3dstudio.usb.UsbAccess
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Objetos com o mesmo ciclo de vida do processo, compartilhados entre telas:
 * o Kinect só pode ser aberto uma vez.
 */
class KinectRepository(app: Application) {
    val log = DiagnosticLog()
    val usb = UsbAccess(app)
    val device = KinectV1Device(usb, log)
    val store = CaptureStore(app)

    /** Serializa operações no dispositivo (abrir, fluxos, fechar, motor). */
    val deviceLock = Mutex()

    /**
     * Protege o uso de handles nativos pela renderização/captura: nenhum handle
     * é fechado enquanto um quadro está sendo desenhado ou salvo.
     */
    val renderLock = Mutex()

    suspend fun closeDevice() {
        renderLock.withLock { deviceLock.withLock { device.close() } }
    }

    init {
        // o processo do jogo (":game") não mexe nas capturas do app
        val removed = if (K3DApp.isGameProcess(app)) 0 else store.cleanupTemporary()
        if (removed > 0) log.warn("$removed captura(s) incompleta(s) de uma sessão interrompida foram removidas")
    }
}

class K3DApp : Application() {
    val repo: KinectRepository by lazy { KinectRepository(this) }

    override fun onCreate() {
        super.onCreate()
        com.kinect3dstudio.diagnostics.CrashReport.install(this)
    }

    companion object {
        /** true no processo ":game" (GameActivity, jogo Expedição Ativa). */
        fun isGameProcess(app: Application): Boolean {
            val name = if (android.os.Build.VERSION.SDK_INT >= 28) Application.getProcessName()
            else runCatching { java.io.File("/proc/self/cmdline").readText().trimEnd('\u0000') }.getOrDefault("")
            return name.endsWith(":game")
        }
    }
}
