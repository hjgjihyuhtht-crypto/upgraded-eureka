package com.kinect3dstudio.device

import com.kinect3dstudio.usb.DiscoveryResult

/** Fluxos de sensor que um driver pode oferecer. */
enum class SensorStream { DEPTH, RGB, IR }

/** Combinação de fluxos pedida ao driver. */
data class StreamSelection(val depth: Boolean, val video: SensorStream?) {
    init {
        require(video == null || video != SensorStream.DEPTH) { "video deve ser RGB, IR ou null" }
    }
}

data class SensorInfo(
    val modelName: String,
    val driverName: String,
    val availableStreams: Set<SensorStream>,
    /** Combinações que o hardware NÃO consegue transmitir ao mesmo tempo, com o motivo. */
    val streamRestrictions: List<String>,
    val hasMotor: Boolean,
    val notes: List<String>,
)

/** Estatísticas medidas em quadros reais recebidos do sensor. */
data class StreamStats(
    val depthFrames: Long,
    val videoFrames: Long,
    val depthFps: Double,
    val videoFps: Double,
    val depthValidRatio: Double,
    val depthMinMm: Double,
    val depthMaxMm: Double,
    val depthCenterMm: Double,
    val videoMeanIntensity: Double,
    /** Diferença entre a chegada dos últimos quadros de cada fluxo; null se só um fluxo. */
    val syncDeltaMs: Double?,
    val deviceLost: Boolean,
)

data class MotorState(val tiltDegrees: Double, val accelX: Double, val accelY: Double, val accelZ: Double, val status: Int)

/** Erro com motivo técnico apresentado ao usuário. */
sealed class DeviceError(val message: String) {
    class NotFound(msg: String) : DeviceError(msg)
    class Unsupported(msg: String) : DeviceError(msg)
    class PermissionDenied(msg: String) : DeviceError(msg)
    class PowerOrCabling(msg: String) : DeviceError(msg)
    class OpenFailed(msg: String) : DeviceError(msg)
    class StreamFailed(msg: String) : DeviceError(msg)
    class Disconnected(msg: String) : DeviceError(msg)
    class NativeUnavailable(msg: String) : DeviceError(msg)
}

sealed interface DeviceResult<out T> {
    data class Ok<T>(val value: T) : DeviceResult<T>
    data class Err(val error: DeviceError) : DeviceResult<Nothing>
}

/**
 * Camada de abstração por modelo de Kinect (seção 3.1 do prompt mestre).
 * Cada família tem seu próprio driver; nunca se usa a API de uma geração na outra.
 *
 * Etapa 2: o recebimento de quadros expõe estatísticas medidas sobre os quadros
 * reais (FPS, pixels válidos, distâncias). A entrega dos buffers completos para
 * visualização e gravação entra na Etapa 3 (sensores).
 */
interface KinectDeviceInterface {
    /** Enumera o que está conectado e classifica (sem abrir nada). */
    fun enumerate(): DiscoveryResult

    /** Pede ao Android permissão para cada sub-dispositivo USB necessário. */
    suspend fun requestAuthorization(): DeviceResult<Unit>

    /** Abre o dispositivo e inicializa os sensores (leitura de registros e calibração). */
    suspend fun open(): DeviceResult<SensorInfo>

    fun sensorInfo(): SensorInfo?

    fun startStreams(selection: StreamSelection): DeviceResult<Unit>
    fun stopStreams()

    /** Estatísticas dos quadros recebidos desde o último startStreams. */
    fun readStats(): StreamStats?

    fun close()

    /** Último erro ocorrido, para diagnóstico. */
    val lastError: DeviceError?
}

/** Interface opcional para modelos com motor/LED/acelerômetro acessíveis. */
interface KinectMotorControl {
    fun setLed(mode: Int): Boolean
    fun setTilt(degrees: Double): Boolean
    fun readMotor(): MotorState?
}
