package com.kinect3dstudio.game

import android.app.NativeActivity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Process
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.kinect3dstudio.K3DApp
import com.kinect3dstudio.device.DeviceResult
import com.kinect3dstudio.device.SensorStream
import com.kinect3dstudio.device.StreamSelection
import com.kinect3dstudio.nativebridge.NativeGame
import com.kinect3dstudio.nativebridge.NativeKinect
import com.kinect3dstudio.usb.DiscoveryResult
import com.kinect3dstudio.usb.KinectUsbIds
import com.kinect3dstudio.usb.UsbEvent
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/**
 * Expedição Ativa no Android: o mesmo jogo da versão de PC (pc/src), desenhado com raylib
 * nesta NativeActivity. Roda no processo ":game" (veja o AndroidManifest): o raylib guarda estado
 * global, então cada partida começa num processo novo, e uma falha no jogo não derruba o app.
 *
 * Este lado Kotlin abre o Kinect (UsbManager + libfreenect, o mesmo driver da tela de sensores),
 * liga profundidade + RGB e entrega a sessão ao jogo; também fecha tudo ao sair.
 */
class GameActivity : NativeActivity() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val repo by lazy { (application as K3DApp).repo }
    private var simulation = false

    override fun onCreate(savedInstanceState: Bundle?) {
        // As bibliotecas precisam estar carregadas antes do NativeActivity iniciar o código nativo.
        NativeGame.loadError
        NativeGame.nativeSetLogPath(com.kinect3dstudio.diagnostics.CrashReport.gameLogPath(this))
        simulation = intent.getBooleanExtra(EXTRA_SIMULATION, false)
        if (simulation) NativeGame.nativeSetSimulation() else NativeGame.nativeSetWaiting("Procurando o Kinect…")
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        hideSystemBars()
        if (!simulation) {
            scope.launch { openKinect() }
            scope.launch { watchRetry() }
            scope.launch { repo.usb.events().collect { onUsbEvent(it) } }
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    private fun hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    private suspend fun openKinect() {
        val device = repo.device
        val log = repo.log
        NativeGame.nativeSetWaiting("Procurando o Kinect…")
        val message: String? = repo.deviceLock.withLock {
            if (device.isOpen) return@withLock null
            when (val d = device.enumerate()) {
                is DiscoveryResult.KinectV1 -> Unit
                is DiscoveryResult.Unsupported -> return@withLock d.reason
                is DiscoveryResult.V1CameraMissing -> return@withLock d.reason
                else -> return@withLock "Nenhum Kinect encontrado. Conecte o Kinect v1 pelo adaptador OTG com a fonte de 12 V ligada."
            }
            NativeGame.nativeSetWaiting("Pedindo permissão de USB…")
            (device.requestAuthorization() as? DeviceResult.Err)?.let { return@withLock it.error.message }
            NativeGame.nativeSetWaiting("Abrindo o Kinect…")
            (device.open() as? DeviceResult.Err)?.let { return@withLock it.error.message }
            val started = withContext(Dispatchers.IO) { device.startStreams(StreamSelection(depth = true, video = SensorStream.RGB)) }
            (started as? DeviceResult.Err)?.let {
                withContext(Dispatchers.IO) { device.close() }
                return@withLock it.error.message
            }
            if (device.sensorInfo()?.hasMotor == true) device.setLed(NativeKinect.LED_GREEN)
            null
        }
        if (message != null) {
            log.warn("Jogo: $message")
            NativeGame.nativeSetFailed(message)
            return
        }
        val handle = device.source?.handle ?: 0L
        NativeGame.nativeSetSource(handle, device.sensorInfo()?.modelName ?: "Kinect v1")
        log.info("Jogo: Kinect entregue ao Expedição Ativa")
    }

    private suspend fun watchRetry() {
        while (scope.isActive) {
            delay(300)
            if (NativeGame.nativeTakeRetryRequest() && !repo.device.isOpen) openKinect()
        }
    }

    private fun onUsbEvent(e: UsbEvent) {
        val id = KinectUsbIds.identify(e.device.vendorId, e.device.productId)
        if (id?.role != KinectUsbIds.Role.CAMERA) return
        when (e) {
            is UsbEvent.Detached -> scope.launch {
                closeKinect("O Kinect foi desconectado. Reconecte o cabo OTG e confira a fonte de 12 V.")
            }
            is UsbEvent.Attached -> scope.launch { if (!repo.device.isOpen) openKinect() }
        }
    }

    /** Tira a fonte do jogo (espera ele soltar) e só então fecha o Kinect. */
    private suspend fun closeKinect(reason: String) {
        withContext(Dispatchers.IO) { NativeGame.nativeRevokeSource(reason, 1500) }
        repo.closeDevice()
    }

    override fun onDestroy() {
        scope.cancel()
        // super.onDestroy() espera a thread nativa do jogo terminar; depois disso nada mais lê o Kinect.
        super.onDestroy()
        runBlocking { if (repo.device.isOpen) repo.closeDevice() }
        if (isFinishing) {
            // processo exclusivo do jogo: sair garante que a próxima partida começa do zero (raylib)
            Process.killProcess(Process.myPid())
        }
    }

    companion object {
        const val EXTRA_SIMULATION = "simulacao"

        fun intent(context: Context, simulation: Boolean): Intent =
            Intent(context, GameActivity::class.java).putExtra(EXTRA_SIMULATION, simulation)
    }
}
