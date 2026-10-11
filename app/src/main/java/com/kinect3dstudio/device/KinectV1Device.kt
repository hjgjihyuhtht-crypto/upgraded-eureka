package com.kinect3dstudio.device

import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import com.kinect3dstudio.diagnostics.DiagnosticLog
import com.kinect3dstudio.nativebridge.NativeKinect
import com.kinect3dstudio.sensors.FrameSourceHandle
import com.kinect3dstudio.usb.DiscoveryResult
import com.kinect3dstudio.usb.KinectDiscovery
import com.kinect3dstudio.usb.KinectUsbIds
import com.kinect3dstudio.usb.UsbAccess
import com.kinect3dstudio.usb.UsbDescriptorParser
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Driver do Kinect v1 (Xbox 360 1414/1473 e Kinect for Windows v1) via
 * libfreenect + libusb, usando os file descriptors concedidos pelo Android.
 */
class KinectV1Device(
    private val usb: UsbAccess,
    private val log: DiagnosticLog,
) : KinectDeviceInterface, KinectMotorControl {

    private var discovery: DiscoveryResult.KinectV1? = null
    private var cameraConn: UsbDeviceConnection? = null
    private var motorConn: UsbDeviceConnection? = null
    private var handle = 0L
    private var info: SensorInfo? = null

    override var lastError: DeviceError? = null
        private set

    val isOpen get() = handle != 0L

    /** Fonte de quadros ao vivo (renderização, captura, gravação), enquanto aberto. */
    val source: FrameSourceHandle? get() = if (isOpen) FrameSourceHandle.live(handle) else null

    /** Fluxos atualmente transmitindo, ou null se parados. */
    var activeSelection: StreamSelection? = null
        private set

    private fun <T> fail(e: DeviceError): DeviceResult<T> {
        lastError = e
        log.error(e.message)
        return DeviceResult.Err(e)
    }

    override fun enumerate(): DiscoveryResult {
        val r = KinectDiscovery.classify(usb.summaries())
        discovery = r as? DiscoveryResult.KinectV1
        return r
    }

    override suspend fun requestAuthorization(): DeviceResult<Unit> {
        val d = discovery ?: return fail(DeviceError.NotFound("Nenhuma câmera de Kinect v1 detectada"))
        val camera = usb.find(d.camera) ?: return fail(DeviceError.Disconnected("A câmera saiu do barramento"))
        if (!usb.requestPermission(camera)) {
            return fail(DeviceError.PermissionDenied("Permissão USB negada para a câmera do Kinect"))
        }
        log.info("Permissão concedida: câmera ${d.camera.deviceName}")
        // O motor só é controlável diretamente no modelo 1414; pedimos se existir.
        d.motor?.let { usb.find(it) }?.let { motor ->
            if (usb.requestPermission(motor)) log.info("Permissão concedida: motor ${motor.deviceName}")
            else log.warn("Permissão do motor negada: inclinação, LED e acelerômetro ficarão indisponíveis")
        }
        return DeviceResult.Ok(Unit)
    }

    override suspend fun open(): DeviceResult<SensorInfo> = withContext(Dispatchers.IO) {
        NativeKinect.loadError?.let { return@withContext fail(DeviceError.NativeUnavailable(it)) }
        if (isOpen) return@withContext DeviceResult.Ok(info!!)
        val d = discovery ?: return@withContext fail(DeviceError.NotFound("Execute a detecção primeiro"))
        val camera: UsbDevice = usb.find(d.camera)
            ?: return@withContext fail(DeviceError.Disconnected("A câmera saiu do barramento"))
        if (!usb.hasPermission(camera)) {
            return@withContext fail(DeviceError.PermissionDenied("Sem permissão USB para a câmera"))
        }

        val cam = usb.open(camera)
            ?: return@withContext fail(DeviceError.OpenFailed("UsbManager.openDevice devolveu null para a câmera"))
        cameraConn = cam
        UsbDescriptorParser.parseDevice(cam.rawDescriptors)?.let {
            log.info(
                "Descritor da câmera: USB ${UsbDescriptorParser.bcdToString(it.bcdUsb)}, " +
                    "bcdDevice ${KinectUsbIds.hex(it.bcdDevice)}",
            )
        }
        // Força a liberação de um eventual driver do kernel (gspca_kinect) antes do libusb.
        if (camera.interfaceCount > 0) {
            val iface = camera.getInterface(0)
            if (cam.claimInterface(iface, true)) cam.releaseInterface(iface)
        }

        val motorDevice = d.motor?.let { usb.find(it) }?.takeIf { usb.hasPermission(it) }
        motorConn = motorDevice?.let { usb.open(it) }

        val h = NativeKinect.nativeOpen(cam.fileDescriptor, motorConn?.fileDescriptor ?: -1)
        drainNativeLog()
        if (h == 0L) {
            val msg = NativeKinect.nativeLastError()
            closeConnections()
            return@withContext fail(DeviceError.OpenFailed("Falha ao inicializar o Kinect: $msg"))
        }
        handle = h
        val di = NativeKinect.nativeDeviceInfo(h)
        val model = KinectUsbIds.v1ModelName(di[0], di[1])
        val motorOk = di[3] == 1
        if (!motorOk) motorConn?.close().also { motorConn = null }

        val notes = buildList {
            add("Profundidade: 640×480, 11 bits brutos a 30 Hz; distância em mm pela calibração de fábrica do próprio Kinect.")
            add("RGB: 640×480 a 30 Hz. Infravermelho: 640×488 em 8 bits a 30 Hz.")
            if (di[2] == 0) {
                add(
                    "Modelo 1473/K4W: o motor e o LED são controlados pelo chip de áudio, que exige o " +
                        "firmware proprietário \"audios.bin\" da Microsoft. Nesta versão eles ficam indisponíveis; " +
                        "câmera, IR e profundidade funcionam normalmente.",
                )
            }
            add("O libfreenect não tem esqueleto: o jogo Expedição Ativa usa o rastreador corporal próprio (silhueta e geometria da profundidade).")
        }
        val sensorInfo = SensorInfo(
            modelName = model,
            driverName = NativeKinect.nativeVersion(),
            availableStreams = setOf(SensorStream.DEPTH, SensorStream.RGB, SensorStream.IR),
            streamRestrictions = listOf(
                "RGB e IR usam o mesmo endpoint de vídeo do Kinect v1: não podem ser transmitidos ao mesmo tempo. " +
                    "Profundidade pode acompanhar qualquer um dos dois.",
            ),
            hasMotor = motorOk,
            notes = notes,
        )
        info = sensorInfo
        log.info("Sensor inicializado: $model")
        DeviceResult.Ok(sensorInfo)
    }

    override fun sensorInfo() = info

    override fun startStreams(selection: StreamSelection): DeviceResult<Unit> {
        if (!isOpen) return fail(DeviceError.StreamFailed("Kinect não está aberto"))
        val video = when (selection.video) {
            SensorStream.RGB -> NativeKinect.VIDEO_RGB
            SensorStream.IR -> NativeKinect.VIDEO_IR
            else -> NativeKinect.VIDEO_NONE
        }
        val ok = NativeKinect.nativeStartStreams(handle, selection.depth, video)
        drainNativeLog()
        activeSelection = if (ok) selection else null
        return if (ok) DeviceResult.Ok(Unit)
        else fail(DeviceError.StreamFailed(NativeKinect.nativeLastError()))
    }

    override fun stopStreams() {
        activeSelection = null
        if (isOpen) NativeKinect.nativeStopStreams(handle)
        drainNativeLog()
    }

    override fun readStats(): StreamStats? {
        if (!isOpen) return null
        val s = NativeKinect.nativeStats(handle)
        drainNativeLog()
        return StreamStats(
            depthFrames = s[NativeKinect.S_DEPTH_FRAMES].toLong(),
            videoFrames = s[NativeKinect.S_VIDEO_FRAMES].toLong(),
            depthFps = s[NativeKinect.S_DEPTH_FPS],
            videoFps = s[NativeKinect.S_VIDEO_FPS],
            depthValidRatio = s[NativeKinect.S_DEPTH_VALID_RATIO],
            depthMinMm = s[NativeKinect.S_DEPTH_MIN_MM],
            depthMaxMm = s[NativeKinect.S_DEPTH_MAX_MM],
            depthCenterMm = s[NativeKinect.S_DEPTH_CENTER_MM],
            videoMeanIntensity = s[NativeKinect.S_VIDEO_MEAN],
            syncDeltaMs = s[NativeKinect.S_SYNC_DELTA_MS].takeIf { it >= 0 },
            deviceLost = s[NativeKinect.S_DEVICE_DEAD] > 0.5,
        )
    }

    override fun setLed(mode: Int) = isOpen && NativeKinect.nativeSetLed(handle, mode) >= 0

    override fun setTilt(degrees: Double) = isOpen && NativeKinect.nativeSetTilt(handle, degrees) >= 0

    override fun readMotor(): MotorState? {
        if (!isOpen) return null
        val m = NativeKinect.nativeReadMotor(handle) ?: return null
        return MotorState(m[0], m[1], m[2], m[3], m[4].toInt())
    }

    override fun close() {
        activeSelection = null
        if (isOpen) {
            NativeKinect.nativeClose(handle)
            handle = 0L
            drainNativeLog()
        }
        closeConnections()
        info = null
    }

    private fun closeConnections() {
        cameraConn?.close(); cameraConn = null
        motorConn?.close(); motorConn = null
    }

    private fun drainNativeLog() {
        if (NativeKinect.loadError != null) return
        NativeKinect.nativeDrainLog().lineSequence().filter { it.isNotBlank() }.forEach { log.native(it) }
    }
}
