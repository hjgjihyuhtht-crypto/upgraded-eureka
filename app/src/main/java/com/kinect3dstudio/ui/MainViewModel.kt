package com.kinect3dstudio.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.kinect3dstudio.K3DApp
import com.kinect3dstudio.device.DeviceResult
import com.kinect3dstudio.device.MotorState
import com.kinect3dstudio.device.SensorInfo
import com.kinect3dstudio.device.SensorStream
import com.kinect3dstudio.device.StreamSelection
import com.kinect3dstudio.device.StreamSelfTest
import com.kinect3dstudio.device.StreamStats
import com.kinect3dstudio.diagnostics.DiagnosticLog
import com.kinect3dstudio.nativebridge.NativeKinect
import com.kinect3dstudio.usb.BandwidthEstimate
import com.kinect3dstudio.usb.DiscoveryResult
import com.kinect3dstudio.usb.KinectUsbIds
import com.kinect3dstudio.usb.UsbBandwidth
import com.kinect3dstudio.usb.UsbEvent
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.withLock

sealed interface ConnectionState {
    data object Idle : ConnectionState
    data object Requesting : ConnectionState
    data object Opening : ConnectionState
    data object Connected : ConnectionState
    data class Failed(val reason: String) : ConnectionState
}

data class UsbDeviceRow(val name: String, val vid: Int, val pid: Int, val label: String, val endpoints: String)

data class UiState(
    val hostSupported: Boolean = true,
    val nativeError: String? = null,
    val discovery: DiscoveryResult = DiscoveryResult.NothingFound,
    val usbDevices: List<UsbDeviceRow> = emptyList(),
    val bandwidth: BandwidthEstimate? = null,
    val connection: ConnectionState = ConnectionState.Idle,
    val sensorInfo: SensorInfo? = null,
    val testRunning: Boolean = false,
    val testPhase: String? = null,
    val liveStats: StreamStats? = null,
    val testResults: List<StreamSelfTest.PhaseResult> = emptyList(),
    val motor: MotorState? = null,
    val motorMessage: String? = null,
    /** O app ou o jogo fechou de forma inesperada da última vez (texto para copiar). */
    val crashReport: String? = null,
)

class MainViewModel(app: Application) : AndroidViewModel(app) {
    private val repo = (app as K3DApp).repo
    val log: DiagnosticLog = repo.log
    private val usb = repo.usb
    private val device = repo.device
    private val deviceLock = repo.deviceLock
    private var testJob: Job? = null
    private var reconnectWanted = false

    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state

    init {
        _state.update {
            it.copy(
                hostSupported = usb.hostSupported, nativeError = NativeKinect.loadError,
                crashReport = com.kinect3dstudio.diagnostics.CrashReport.read(app),
            )
        }
        if (!usb.hostSupported) log.error("Este aparelho não declara suporte a USB Host (OTG). O Kinect não pode ser conectado.")
        NativeKinect.loadError?.let { log.error(it) }
        refresh()
        viewModelScope.launch { usb.events().collect { onUsbEvent(it) } }
    }

    fun refresh() {
        val devices = usb.devices()
        val rows = devices.map { d ->
            val id = KinectUsbIds.identify(d.vendorId, d.productId)
            val label = id?.let { "${it.family.displayName} — ${it.role.name.lowercase()}" } ?: "Outro dispositivo"
            val eps = usb.endpointsOf(d)
            val iso = eps.count { it.isIsochronous }
            UsbDeviceRow(d.deviceName, d.vendorId, d.productId, label, "${d.interfaceCount} interface(s), ${eps.size} endpoint(s), $iso isócrono(s)")
        }
        val discovery = device.enumerate()
        val bandwidth = (discovery as? DiscoveryResult.KinectV1)?.let { k ->
            usb.find(k.camera)?.let { UsbBandwidth.estimateHighSpeed(usb.endpointsOf(it)) }
        }
        _state.update { it.copy(usbDevices = rows, discovery = discovery, bandwidth = bandwidth) }
        when (discovery) {
            is DiscoveryResult.NothingFound -> log.info("Nenhum Kinect no USB (${devices.size} dispositivo(s) conectado(s)).")
            is DiscoveryResult.KinectV1 -> log.info(
                "Detectado ${discovery.family.displayName}: câmera ${discovery.camera.deviceName}" +
                    (discovery.motor?.let { ", motor ${it.deviceName}" } ?: "") +
                    (discovery.audio?.let { ", áudio ${it.deviceName}" } ?: ""),
            )
            is DiscoveryResult.V1CameraMissing -> log.warn(discovery.reason)
            is DiscoveryResult.Unsupported -> log.warn(discovery.reason)
        }
        bandwidth?.let {
            log.info(
                "Banda isócrona exigida: %.1f MB/s (%.0f%% do orçamento periódico USB 2.0)"
                    .format(it.megabytesPerSecond, it.periodicBudgetShare * 100),
            )
        }
    }

    fun connect() {
        viewModelScope.launch {
            deviceLock.withLock {
                if (device.isOpen) return@withLock
                refresh()
                val d = _state.value.discovery
                if (d !is DiscoveryResult.KinectV1) {
                    val reason = when (d) {
                        is DiscoveryResult.Unsupported -> d.reason
                        is DiscoveryResult.V1CameraMissing -> d.reason
                        else -> "Nenhum Kinect detectado. Conecte o Kinect pelo adaptador OTG com a fonte de 12 V ligada."
                    }
                    _state.update { it.copy(connection = ConnectionState.Failed(reason)) }
                    return@withLock
                }
                _state.update { it.copy(connection = ConnectionState.Requesting) }
                val auth = device.requestAuthorization()
                if (auth is DeviceResult.Err) {
                    _state.update { it.copy(connection = ConnectionState.Failed(auth.error.message)) }
                    return@withLock
                }
                _state.update { it.copy(connection = ConnectionState.Opening) }
                when (val r = device.open()) {
                    is DeviceResult.Ok -> {
                        reconnectWanted = true
                        _state.update { it.copy(connection = ConnectionState.Connected, sensorInfo = r.value) }
                        r.value.notes.forEach(log::info)
                        if (r.value.hasMotor) device.setLed(NativeKinect.LED_GREEN)
                    }
                    is DeviceResult.Err -> _state.update { it.copy(connection = ConnectionState.Failed(r.error.message)) }
                }
            }
        }
    }

    fun clearCrashReport() {
        com.kinect3dstudio.diagnostics.CrashReport.clear(getApplication<Application>())
        _state.update { it.copy(crashReport = null) }
    }

    private var reconnectAfterGame = false

    /**
     * O jogo roda em outro processo e abre o Kinect por conta própria: solta o dispositivo aqui
     * antes de abrir o jogo (o USB só pode ter um dono por vez).
     */
    suspend fun releaseForGame() {
        testJob?.cancel()
        reconnectAfterGame = device.isOpen
        reconnectWanted = false
        if (device.isOpen) {
            repo.closeDevice()
            log.info("Kinect liberado para o jogo Expedição Ativa")
        }
        _state.update {
            it.copy(connection = ConnectionState.Idle, sensorInfo = null, liveStats = null, motor = null, testRunning = false)
        }
    }

    /** Volta do jogo: reconecta se o Kinect estava conectado antes. */
    fun onReturnFromGame() {
        refresh()
        // o jogo pode ter fechado com erro: mostra o relatório na tela inicial
        _state.update { it.copy(crashReport = com.kinect3dstudio.diagnostics.CrashReport.read(getApplication<Application>())) }
        if (!reconnectAfterGame) return
        reconnectAfterGame = false
        viewModelScope.launch {
            // o processo do jogo ainda pode estar soltando o USB
            kotlinx.coroutines.delay(1500)
            reconnectWanted = true
            connect()
        }
    }

    fun disconnect() {
        reconnectWanted = false
        testJob?.cancel()
        viewModelScope.launch {
            repo.closeDevice()
            log.info("Desconectado pelo usuário")
            _state.update {
                it.copy(connection = ConnectionState.Idle, sensorInfo = null, liveStats = null, motor = null, testRunning = false)
            }
        }
    }

    private fun onUsbEvent(e: UsbEvent) {
        when (e) {
            is UsbEvent.Attached -> {
                log.info("USB conectado: ${e.device.deviceName} (${KinectUsbIds.hex(e.device.vendorId)}:${KinectUsbIds.hex(e.device.productId)})")
                refresh()
                val id = KinectUsbIds.identify(e.device.vendorId, e.device.productId)
                if (reconnectWanted && !device.isOpen && id?.role == KinectUsbIds.Role.CAMERA) {
                    log.info("Tentando reconectar automaticamente…")
                    connect()
                }
            }
            is UsbEvent.Detached -> {
                log.warn("USB removido: ${e.device.deviceName}")
                val d = _state.value.discovery as? DiscoveryResult.KinectV1
                if (device.isOpen && d != null && e.device.deviceName == d.camera.deviceName) {
                    testJob?.cancel()
                    viewModelScope.launch {
                        repo.closeDevice()
                        _state.update {
                            it.copy(
                                connection = ConnectionState.Failed(
                                    "O Kinect foi desconectado. Verifique o cabo e a fonte de 12 V; " +
                                        "a reconexão será automática quando ele voltar.",
                                ),
                                sensorInfo = null, liveStats = null, motor = null, testRunning = false,
                            )
                        }
                    }
                }
                refresh()
            }
        }
    }

    /** Usado quando o app é aberto pelo intent USB_DEVICE_ATTACHED (permissão já concedida). */
    fun onLaunchedByUsb() {
        refresh()
        if (_state.value.discovery is DiscoveryResult.KinectV1 && !device.isOpen) connect()
    }

    /**
     * Teste de inicialização: transmite fluxos reais e mede o que chega.
     * Fase 1: profundidade + RGB. Fase 2: profundidade + IR.
     */
    fun runSelfTest(secondsPerPhase: Int = 5) {
        if (testJob?.isActive == true || !device.isOpen) return
        if (device.activeSelection != null) {
            log.warn("Feche a tela de sensores antes de rodar o teste de fluxos")
            return
        }
        testJob = viewModelScope.launch {
            _state.update { it.copy(testRunning = true, testResults = emptyList()) }
            val phases = listOf(
                "Profundidade + RGB" to StreamSelection(depth = true, video = SensorStream.RGB),
                "Profundidade + Infravermelho" to StreamSelection(depth = true, video = SensorStream.IR),
            )
            val results = mutableListOf<StreamSelfTest.PhaseResult>()
            try {
                for ((label, sel) in phases) {
                    _state.update { it.copy(testPhase = label) }
                    log.info("Teste: iniciando $label")
                    val started = deviceLock.withLock { device.startStreams(sel) }
                    if (started is DeviceResult.Err) break
                    val t0 = System.nanoTime()
                    var last: StreamStats? = null
                    while (isActive && System.nanoTime() - t0 < secondsPerPhase * 1_000_000_000L) {
                        delay(250)
                        last = device.readStats()
                        _state.update { it.copy(liveStats = last) }
                        if (last?.deviceLost == true) break
                    }
                    val seconds = (System.nanoTime() - t0) / 1e9
                    deviceLock.withLock { device.stopStreams() }
                    val res = StreamSelfTest.evaluate(label, sel, last ?: break, seconds)
                    results += res
                    res.findings.forEach { f ->
                        if (res.verdict == StreamSelfTest.Verdict.OK) log.info(f) else log.warn(f)
                    }
                    _state.update { it.copy(testResults = results.toList()) }
                    if (res.stats.deviceLost) break
                }
            } finally {
                _state.update { it.copy(testRunning = false, testPhase = null) }
            }
        }
    }

    fun setLed(mode: Int) = viewModelScope.launch {
        val ok = deviceLock.withLock { device.setLed(mode) }
        _state.update { it.copy(motorMessage = if (ok) "LED atualizado" else "Falha ao mudar o LED") }
    }

    fun applyTilt(degrees: Double) = viewModelScope.launch {
        val ok = deviceLock.withLock { device.setTilt(degrees) }
        _state.update { it.copy(motorMessage = if (ok) "Inclinação enviada: %.0f°".format(degrees) else "Falha ao inclinar") }
        delay(1500) // o motor leva cerca de 1 s para se mover
        readMotor()
    }

    fun readMotor() = viewModelScope.launch {
        val m = deviceLock.withLock { device.readMotor() }
        _state.update { it.copy(motor = m, motorMessage = if (m == null) "Leitura do motor indisponível" else it.motorMessage) }
    }

    override fun onCleared() {
        testJob?.cancel()
        // O Kinect pertence ao KinectRepository (ciclo de vida do processo):
        // não é fechado aqui para não derrubar a tela de sensores.
    }
}
