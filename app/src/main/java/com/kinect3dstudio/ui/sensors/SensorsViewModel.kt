package com.kinect3dstudio.ui.sensors

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.kinect3dstudio.BuildConfig
import com.kinect3dstudio.K3DApp
import com.kinect3dstudio.device.DeviceResult
import com.kinect3dstudio.device.SensorStream
import com.kinect3dstudio.device.StreamSelection
import com.kinect3dstudio.nativebridge.NativeKinect
import com.kinect3dstudio.sensors.DepthProbe
import com.kinect3dstudio.sensors.DepthViewParams
import com.kinect3dstudio.sensors.FrameSourceHandle
import com.kinect3dstudio.sensors.SourceStats
import com.kinect3dstudio.sensors.VideoKind
import com.kinect3dstudio.sensors.VideoViewParams
import com.kinect3dstudio.storage.StorageFormat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

enum class StreamMode(val label: String, val selection: StreamSelection) {
    DEPTH_RGB("Profundidade + RGB", StreamSelection(depth = true, video = SensorStream.RGB)),
    DEPTH_IR("Profundidade + IR", StreamSelection(depth = true, video = SensorStream.IR)),
    DEPTH_ONLY("Só profundidade", StreamSelection(depth = true, video = null)),
}

enum class ViewLayout(val label: String) {
    SIDE_BY_SIDE("Lado a lado"),
    DEPTH("Profundidade"),
    VIDEO("Vídeo"),
    ALIGNED("RGB alinhado"),
}

enum class SourceKind { NONE, LIVE, PLAYBACK }

data class FrameImage(val bitmap: ImageBitmap, val seq: Long, val width: Int, val height: Int)

data class SensorsUiState(
    val source: SourceKind = SourceKind.NONE,
    val playbackName: String? = null,
    val mode: StreamMode = StreamMode.DEPTH_RGB,
    val layout: ViewLayout = ViewLayout.SIDE_BY_SIDE,
    val depth: DepthViewParams = DepthViewParams(),
    val video: VideoViewParams = VideoViewParams(),
    val streaming: Boolean = false,
    val error: String? = null,
    val stats: SourceStats? = null,
    val probe: DepthProbe? = null,
    val recordingFile: String? = null,
    val recordingStartMs: Long = 0,
    val freeBytes: Long = 0,
    val busy: Boolean = false,
    val message: String? = null,
    val playbackSpeed: Double = 1.0,
    val playbackPaused: Boolean = false,
    val playbackLoop: Boolean = true,
) {
    val recording get() = recordingFile != null
}

/**
 * Tela de sensores: fluxos ao vivo do Kinect ou reprodução de gravação.
 * A renderização roda numa única thread de fundo (as funções nativas de render
 * não são reentrantes) e publica bitmaps em buffer triplo.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class SensorsViewModel(app: Application) : AndroidViewModel(app) {
    private val repo = (app as K3DApp).repo
    private val log = repo.log
    private val renderDispatcher = Dispatchers.Default.limitedParallelism(1)

    private val _state = MutableStateFlow(SensorsUiState())
    val state: StateFlow<SensorsUiState> = _state

    private val _depthImage = MutableStateFlow<FrameImage?>(null)
    val depthImage: StateFlow<FrameImage?> = _depthImage
    private val _videoImage = MutableStateFlow<FrameImage?>(null)
    val videoImage: StateFlow<FrameImage?> = _videoImage
    private val _alignedImage = MutableStateFlow<FrameImage?>(null)
    val alignedImage: StateFlow<FrameImage?> = _alignedImage

    private var playback: FrameSourceHandle? = null
    private var renderJob: Job? = null
    private var statsJob: Job? = null

    @Volatile private var forceRedraw = true

    /** Serializa entrar/sair da tela (as chamadas vêm de efeitos de composição). */
    private val transition = kotlinx.coroutines.sync.Mutex()

    private fun currentSource(): FrameSourceHandle? = when (_state.value.source) {
        SourceKind.LIVE -> repo.device.source
        SourceKind.PLAYBACK -> playback
        SourceKind.NONE -> null
    }

    // ------------------------------------------------------------- entrada/saída

    fun enterLive() = viewModelScope.launch { transition.withLock { enterLiveInternal() } }

    private suspend fun enterLiveInternal() {
        stopLoops()
        closePlayback()
        if (!repo.device.isOpen) {
            _state.update { it.copy(source = SourceKind.NONE, error = "Kinect não conectado. Conecte na tela inicial.") }
            return
        }
        _state.update { it.copy(source = SourceKind.LIVE, playbackName = null, error = null) }
        startLiveStreams(_state.value.mode)
        startLoops()
    }

    fun enterPlayback(file: File) = viewModelScope.launch { transition.withLock { enterPlaybackInternal(file) } }

    private suspend fun enterPlaybackInternal(file: File) {
        stopLoops()
        stopLiveStreams()
        closePlayback()
        // O erro nativo é por thread: lê na mesma thread que tentou abrir.
        val (h, why) = withContext(Dispatchers.IO) {
            val opened = FrameSourceHandle.openPlayback(file.absolutePath)
            opened to (if (opened == null) NativeKinect.loadError ?: NativeKinect.nativeLastError() else "")
        }
        if (h == null) {
            _state.update { it.copy(source = SourceKind.NONE, error = "Não foi possível abrir ${file.name}: $why") }
            log.error("Reprodução falhou: $why")
            return
        }
        playback = h
        val st = _state.value
        h.setPlayback(st.playbackSpeed, false, st.playbackLoop)
        _state.update {
            it.copy(source = SourceKind.PLAYBACK, playbackName = file.name, error = null, playbackPaused = false, streaming = true)
        }
        log.info("Reproduzindo gravação (simulação): ${file.name}")
        startLoops()
    }

    /** Chamado ao sair da tela: para gravação, fluxos e libera a reprodução. */
    fun leave() = viewModelScope.launch { transition.withLock { leaveInternal() } }

    private suspend fun leaveInternal() {
        stopRecordingInternal()
        stopLoops()
        stopLiveStreams()
        closePlayback()
        _state.update { it.copy(source = SourceKind.NONE, streaming = false, stats = null, probe = null) }
        _depthImage.value = null; _videoImage.value = null; _alignedImage.value = null
    }

    private suspend fun closePlayback() {
        val p = playback ?: return
        repo.renderLock.withLock { playback = null; p.closePlayback() }
    }

    private suspend fun startLiveStreams(mode: StreamMode) {
        val r = repo.deviceLock.withLock { repo.device.startStreams(mode.selection) }
        when (r) {
            is DeviceResult.Ok -> _state.update { it.copy(streaming = true, error = null) }
            is DeviceResult.Err -> _state.update { it.copy(streaming = false, error = r.error.message) }
        }
        forceRedraw = true
    }

    private suspend fun stopLiveStreams() {
        if (repo.device.activeSelection == null) return
        repo.deviceLock.withLock { repo.device.stopStreams() }
        _state.update { it.copy(streaming = false) }
    }

    // -------------------------------------------------------------- controles

    fun setMode(m: StreamMode) = viewModelScope.launch {
        val st = _state.value
        if (st.mode == m) return@launch
        if (st.recording) { message("Pare a gravação antes de trocar os fluxos"); return@launch }
        _state.update { it.copy(mode = m, probe = null) }
        if (st.source == SourceKind.LIVE) {
            _videoImage.value = null; _alignedImage.value = null
            startLiveStreams(m)
        }
    }

    fun setLayout(l: ViewLayout) { _state.update { it.copy(layout = l) }; forceRedraw = true }
    fun setDepthParams(p: DepthViewParams) { _state.update { it.copy(depth = p) }; forceRedraw = true }
    fun setVideoParams(p: VideoViewParams) { _state.update { it.copy(video = p) }; forceRedraw = true }
    fun clearMessage() = _state.update { it.copy(message = null) }
    private fun message(m: String) = _state.update { it.copy(message = m) }

    fun setPlayback(speed: Double = _state.value.playbackSpeed, paused: Boolean = _state.value.playbackPaused,
                    loop: Boolean = _state.value.playbackLoop) {
        _state.update { it.copy(playbackSpeed = speed, playbackPaused = paused, playbackLoop = loop) }
        playback?.setPlayback(speed, paused, loop)
    }

    fun seek(fraction: Float) { playback?.seek(fraction.toDouble()); forceRedraw = true }

    fun probeAt(x: Int, y: Int) = viewModelScope.launch(Dispatchers.Default) {
        val p = repo.renderLock.withLock { currentSource()?.probe(x, y) }
        _state.update { it.copy(probe = p) }
    }

    // ------------------------------------------------------------------ laços

    private fun startLoops() {
        forceRedraw = true
        renderJob = viewModelScope.launch(renderDispatcher) { renderLoop() }
        statsJob = viewModelScope.launch { statsLoop() }
    }

    private suspend fun stopLoops() {
        renderJob?.cancelAndJoin(); renderJob = null
        statsJob?.cancelAndJoin(); statsJob = null
    }

    private class TripleBuffer(val width: Int, val height: Int) {
        private val bitmaps = Array(3) { Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888) }
        private var published = 0
        var lastSeq = 0L
        /** Buffer de trás: nunca é o publicado nem o anterior a ele. */
        fun back(): Bitmap = bitmaps[(published + 1) % 3]
        fun publish(seq: Long): FrameImage {
            published = (published + 1) % 3
            lastSeq = seq
            return FrameImage(bitmaps[published].asImageBitmap(), seq, width, height)
        }
    }

    private suspend fun renderLoop() {
        var depthBuf: TripleBuffer? = null
        var videoBuf: TripleBuffer? = null
        var alignedBuf: TripleBuffer? = null
        while (kotlinx.coroutines.currentCoroutineContext().isActive) {
            val st = _state.value
            if (forceRedraw) {
                forceRedraw = false
                depthBuf?.lastSeq = 0; videoBuf?.lastSeq = 0; alignedBuf?.lastSeq = 0
            }
            val wantDepth = st.layout == ViewLayout.SIDE_BY_SIDE || st.layout == ViewLayout.DEPTH
            val wantVideo = st.layout == ViewLayout.SIDE_BY_SIDE || st.layout == ViewLayout.VIDEO
            val wantAligned = st.layout == ViewLayout.ALIGNED

            repo.renderLock.withLock {
                val src = currentSource() ?: return@withLock
                val stats = st.stats
                if (wantDepth) {
                    val b = depthBuf ?: TripleBuffer(640, 480).also { depthBuf = it }
                    val seq = src.renderDepth(b.back(), st.depth, b.lastSeq)
                    if (seq > 0) _depthImage.value = b.publish(seq)
                }
                if (wantVideo && stats != null && stats.videoKind != VideoKind.NONE && stats.videoWidth > 0) {
                    var b = videoBuf
                    if (b == null || b.width != stats.videoWidth || b.height != stats.videoHeight) {
                        b = TripleBuffer(stats.videoWidth, stats.videoHeight).also { videoBuf = it }
                    }
                    val seq = src.renderVideo(b.back(), st.video, b.lastSeq)
                    if (seq > 0) _videoImage.value = b.publish(seq)
                }
                if (wantAligned && stats?.videoKind == VideoKind.RGB) {
                    val b = alignedBuf ?: TripleBuffer(640, 480).also { alignedBuf = it }
                    val seq = src.renderAligned(b.back(), st.video, b.lastSeq)
                    if (seq > 0) _alignedImage.value = b.publish(seq)
                }
            }
            delay(10)
        }
    }

    private suspend fun statsLoop() {
        while (kotlinx.coroutines.currentCoroutineContext().isActive) {
            val stats = repo.renderLock.withLock { currentSource()?.stats() }
            val free = repo.store.freeBytes()
            _state.update { it.copy(stats = stats, freeBytes = free) }
            if (stats != null) {
                if (stats.stream.deviceLost && _state.value.source == SourceKind.LIVE) {
                    _state.update { it.copy(error = "O Kinect parou de responder (cabo ou fonte de 12 V).", streaming = false) }
                }
                if (_state.value.recording) {
                    when {
                        stats.recording.ioError -> stopRecordingInternal("Gravação interrompida: erro de escrita (armazenamento cheio?)")
                        free < StorageFormat.MIN_FREE_TO_CONTINUE -> stopRecordingInternal("Gravação interrompida: espaço livre abaixo de ${StorageFormat.formatBytes(StorageFormat.MIN_FREE_TO_CONTINUE)}")
                    }
                }
            }
            if (NativeKinect.loadError == null) {
                NativeKinect.nativeDrainLog().lineSequence().filter { it.isNotBlank() }.forEach { log.native(it) }
            }
            delay(250)
        }
    }

    // ------------------------------------------------------- captura de quadro

    fun capture() = viewModelScope.launch(Dispatchers.IO) {
        if (_state.value.busy) return@launch
        _state.update { it.copy(busy = true) }
        val st = _state.value
        val tmp = repo.store.newCaptureTempDir()
        try {
            val result = repo.renderLock.withLock {
                val src = currentSource() ?: error("Nenhuma fonte de quadros ativa")
                val nativeJson = src.capture(tmp.absolutePath, st.depth.filter) ?: error(src.lastError())
                // Prévias PNG: profundidade com o mapa de cor atual; vídeo SEM ajustes de visualização.
                val previews = mutableListOf<String>()
                val d = Bitmap.createBitmap(640, 480, Bitmap.Config.ARGB_8888)
                if (src.renderDepth(d, st.depth, 0) > 0) { savePng(d, File(tmp, "depth_visualizacao.png")); previews += "depth_visualizacao.png" }
                val s = src.stats()
                if (s.videoKind != VideoKind.NONE && s.videoWidth > 0) {
                    val v = Bitmap.createBitmap(s.videoWidth, s.videoHeight, Bitmap.Config.ARGB_8888)
                    val name = if (s.videoKind == VideoKind.RGB) "rgb.png" else "ir.png"
                    if (src.renderVideo(v, VideoViewParams(), 0) > 0) { savePng(v, File(tmp, name)); previews += name }
                }
                Pair(nativeJson, previews)
            }
            val meta = JSONObject(result.first)
            meta.put("app", "Kinect 3D Studio ${BuildConfig.VERSION_NAME}")
            meta.put("captured_at", isoNow())
            meta.put("source", if (st.source == SourceKind.PLAYBACK) "reprodução de gravação: ${st.playbackName}" else "Kinect ao vivo")
            repo.device.sensorInfo()?.let { meta.put("sensor_model", it.modelName) }
            meta.put("stream_mode", st.mode.label)
            meta.put("depth_view", JSONObject().put("min_mm", st.depth.minMm).put("max_mm", st.depth.maxMm)
                .put("colormap", if (st.depth.colormap == NativeKinect.COLORMAP_TURBO) "turbo" else "cinza")
                .put("filter", filterName(st.depth.filter)))
            meta.put("previews_png", org.json.JSONArray(result.second))
            meta.put("notes", "depth_raw.pgm guarda o valor original do sensor (11 bits). depth_mm.pgm é a conversão pela calibração de fábrica. " +
                "Ajustes de brilho/contraste não são aplicados aos arquivos salvos.")
            File(tmp, "metadata.json").writeText(meta.toString(2))
            val final = repo.store.commitCapture(tmp)
            log.info("Captura salva: ${final.name}")
            message("Captura salva: ${final.name}")
        } catch (t: Throwable) {
            tmp.deleteRecursively()
            log.error("Falha na captura: ${t.message}")
            message("Falha na captura: ${t.message}")
        } finally {
            _state.update { it.copy(busy = false) }
        }
    }

    private fun savePng(b: Bitmap, f: File) = FileOutputStream(f).use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }

    // ------------------------------------------------------------------ gravação

    fun toggleRecording() = viewModelScope.launch(Dispatchers.IO) {
        if (_state.value.recording) { stopRecordingInternal(); return@launch }
        val st = _state.value
        if (st.source != SourceKind.LIVE) { message("A gravação só é possível com o Kinect ao vivo"); return@launch }
        val free = repo.store.freeBytes()
        if (free < StorageFormat.MIN_FREE_TO_START) {
            message("Espaço insuficiente: ${StorageFormat.formatBytes(free)} livres (mínimo ${StorageFormat.formatBytes(StorageFormat.MIN_FREE_TO_START)})")
            return@launch
        }
        val src = repo.device.source ?: return@launch
        val file = repo.store.newRecordingFile()
        val sel = st.mode.selection
        val meta = JSONObject()
            .put("app", "Kinect 3D Studio ${BuildConfig.VERSION_NAME}")
            .put("started_at", isoNow())
            .put("sensor_model", repo.device.sensorInfo()?.modelName ?: "desconhecido")
            .put("stream_mode", st.mode.label)
            .put("depth", sel.depth)
            .put("video", sel.video?.name ?: JSONObject.NULL)
            .put("intrinsics", JSONObject(src.intrinsicsJson().takeIf { it != "null" } ?: "{}"))
        val ok = repo.renderLock.withLock { src.startRecording(file.absolutePath, meta.toString(), sel.depth, sel.video != null) }
        if (!ok) { message("Não foi possível iniciar a gravação: ${src.lastError()}"); return@launch }
        val rate = StorageFormat.bytesPerSecond(sel.depth, sel.video?.name)
        log.info("Gravação iniciada: ${file.name} (~%.0f MB/s, cabe ~%.0f min)".format(rate / 1e6, StorageFormat.minutesAvailable(free, rate)))
        _state.update { it.copy(recordingFile = file.name, recordingStartMs = System.currentTimeMillis()) }
    }

    private suspend fun stopRecordingInternal(reason: String? = null) {
        val name = _state.value.recordingFile ?: return
        val stats = repo.renderLock.withLock {
            val src = currentSource()
            src?.stopRecording()
            src?.stats()?.recording
        }
        _state.update { it.copy(recordingFile = null) }
        val summary = stats?.let {
            "${it.framesWritten} quadros, ${StorageFormat.formatBytes(it.bytesWritten)}" +
                if (it.framesDropped > 0) ", ${it.framesDropped} descartados (armazenamento lento)" else ""
        } ?: ""
        val msg = (reason ?: "Gravação salva: $name") + if (summary.isNotEmpty()) " — $summary" else ""
        if (reason != null) log.warn(msg) else log.info(msg)
        message(msg)
    }

    override fun onCleared() {
        // Encerramento síncrono mínimo; a tela normalmente já chamou leave().
        renderJob?.cancel(); statsJob?.cancel()
        playback?.closePlayback(); playback = null
    }

    private companion object {
        fun isoNow(): String = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSSXXX", Locale.ROOT)
            .apply { timeZone = TimeZone.getDefault() }.format(Date())
        fun filterName(f: Int) = when (f) {
            NativeKinect.FILTER_MEDIAN -> "mediana 3x3"
            NativeKinect.FILTER_EDGES -> "remoção de bordas"
            NativeKinect.FILTER_MEDIAN_EDGES -> "mediana + bordas"
            else -> "nenhum"
        }
    }
}
