@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class, androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.kinect3dstudio.ui.sensors

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RangeSlider
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.kinect3dstudio.nativebridge.NativeKinect
import com.kinect3dstudio.sensors.DepthProbe
import com.kinect3dstudio.sensors.VideoKind
import com.kinect3dstudio.sensors.VideoViewParams
import com.kinect3dstudio.storage.StorageFormat
import kotlinx.coroutines.delay

private val RecRed = Color(0xFFD32F2F)
private val SimAmber = Color(0xFFFFB300)

/** Como a tela foi aberta: Kinect ao vivo ou arquivo de gravação. */
sealed interface SensorsEntry {
    data object Live : SensorsEntry
    data class Playback(val path: String) : SensorsEntry
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SensorsScreen(vm: SensorsViewModel, entry: SensorsEntry, onBack: () -> Unit) {
    val s by vm.state.collectAsStateWithLifecycle()
    val depthImg by vm.depthImage.collectAsStateWithLifecycle()
    val videoImg by vm.videoImage.collectAsStateWithLifecycle()
    val alignedImg by vm.alignedImage.collectAsStateWithLifecycle()
    val snack = remember { SnackbarHostState() }

    LaunchedEffect(entry) {
        when (entry) {
            SensorsEntry.Live -> vm.enterLive()
            is SensorsEntry.Playback -> vm.enterPlayback(java.io.File(entry.path))
        }
    }
    DisposableEffect(Unit) { onDispose { vm.leave() } }
    BackHandler { onBack() }
    LaunchedEffect(s.message) {
        s.message?.let { snack.showSnackbar(it); vm.clearMessage() }
    }

    val playback = s.source == SourceKind.PLAYBACK
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(if (playback) "Reprodução (simulação)" else "Sensores ao vivo") },
                navigationIcon = {
                    IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Voltar") }
                },
            )
        },
        snackbarHost = { SnackbarHost(snack) },
    ) { pad ->
        LazyColumn(
            Modifier.fillMaxSize().padding(pad),
            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (playback) item {
                Banner(SimAmber, Color.Black,
                    "SIMULAÇÃO — reproduzindo o arquivo ${s.playbackName}. Estes quadros não vêm do sensor agora.")
            }
            if (s.recording) item { RecordingBanner(s) }
            s.error?.let { item { Banner(MaterialTheme.colorScheme.errorContainer, MaterialTheme.colorScheme.onErrorContainer, it) } }

            if (s.source == SourceKind.LIVE) item { ModeChips(s, vm) }
            item { LayoutChips(s, vm) }
            item { Views(s, vm, depthImg, videoImg, alignedImg) }
            item { ProbeLine(s.probe) }
            item { StatsLine(s) }
            item { Actions(s, vm) }
            if (playback) item { PlaybackControls(s, vm) }
            item { DepthControls(s, vm) }
            if (s.mode != StreamMode.DEPTH_ONLY || playback) item { VideoControls(s, vm) }
            item { InfoCard(s) }
        }
    }
}

@Composable
private fun Banner(bg: Color, fg: Color, text: String) {
    Box(Modifier.fillMaxWidth().background(bg, MaterialTheme.shapes.small).padding(10.dp)) {
        Text(text, color = fg, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
    }
}

@Composable
private fun RecordingBanner(s: SensorsUiState) {
    var now by remember { mutableLongStateOf(System.currentTimeMillis()) }
    LaunchedEffect(Unit) { while (true) { now = System.currentTimeMillis(); delay(500) } }
    val r = s.stats?.recording
    val text = buildString {
        append("● GRAVANDO ")
        append(StorageFormat.formatDuration((now - s.recordingStartMs) / 1000.0))
        r?.let {
            append(" · ${StorageFormat.formatBytes(it.bytesWritten)}")
            if (it.framesDropped > 0) append(" · ${it.framesDropped} descartados")
        }
        append("\nImagens de profundidade e câmera estão sendo salvas neste aparelho.")
    }
    Banner(RecRed, Color.White, text)
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ModeChips(s: SensorsUiState, vm: SensorsViewModel) {
    Column {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            StreamMode.entries.forEach { m ->
                FilterChip(selected = s.mode == m, onClick = { vm.setMode(m) }, label = { Text(m.label) }, enabled = !s.recording)
            }
        }
        Text("RGB e infravermelho usam o mesmo canal do Kinect v1 e não podem ser transmitidos juntos.",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LayoutChips(s: SensorsUiState, vm: SensorsViewModel) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        ViewLayout.entries.forEach { l ->
            FilterChip(selected = s.layout == l, onClick = { vm.setLayout(l) }, label = { Text(l.label) })
        }
    }
}

@Composable
private fun Views(s: SensorsUiState, vm: SensorsViewModel, depth: FrameImage?, video: FrameImage?, aligned: FrameImage?) {
    val videoLabel = when (s.stats?.videoKind) { VideoKind.IR -> "Infravermelho"; VideoKind.RGB -> "RGB"; else -> "Vídeo" }
    val onProbe: (Int, Int) -> Unit = { x, y -> vm.probeAt(x, y) }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        when (s.layout) {
            ViewLayout.SIDE_BY_SIDE -> {
                FrameView(depth, "Profundidade", 640, 480, s.probe, onProbe)
                if (s.stats?.videoKind != VideoKind.NONE) FrameView(video, videoLabel, video?.width ?: 640, video?.height ?: 480, null, null)
            }
            ViewLayout.DEPTH -> FrameView(depth, "Profundidade", 640, 480, s.probe, onProbe)
            ViewLayout.VIDEO -> {
                if (s.stats?.videoKind == VideoKind.NONE) Placeholder("Sem fluxo de vídeo no modo atual")
                else FrameView(video, videoLabel, video?.width ?: 640, video?.height ?: 480, null, null)
            }
            ViewLayout.ALIGNED -> {
                if (s.stats?.videoKind != VideoKind.RGB) Placeholder("O RGB alinhado precisa do modo Profundidade + RGB")
                else {
                    FrameView(aligned, "RGB alinhado à profundidade (calibração de fábrica)", 640, 480, s.probe, onProbe)
                    Text("Cada pixel mostra a cor RGB que corresponde àquele ponto de profundidade. Áreas pretas não têm medida.",
                        style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}

@Composable
private fun Placeholder(text: String) {
    Box(Modifier.fillMaxWidth().aspectRatio(4f / 3f).background(Color(0xFF111111)), contentAlignment = Alignment.Center) {
        Text(text, color = Color.LightGray, fontSize = 13.sp)
    }
}

/** Mostra um quadro preservando a proporção; toque converte para coordenadas do pixel. */
@Composable
private fun FrameView(
    img: FrameImage?, label: String, w: Int, h: Int, probe: DepthProbe?, onTap: ((Int, Int) -> Unit)?,
) {
    Column {
        Text(label, style = MaterialTheme.typography.labelMedium)
        Spacer(Modifier.height(2.dp))
        Box(
            Modifier.fillMaxWidth().aspectRatio(w.toFloat() / h).background(Color(0xFF111111))
                .then(
                    if (onTap != null) Modifier.pointerInput(w, h) {
                        detectTapGestures { o ->
                            val x = (o.x / size.width * w).toInt().coerceIn(0, w - 1)
                            val y = (o.y / size.height * h).toInt().coerceIn(0, h - 1)
                            onTap(x, y)
                        }
                    } else Modifier,
                ),
            contentAlignment = Alignment.Center,
        ) {
            if (img == null) Text("Aguardando quadros…", color = Color.LightGray, fontSize = 13.sp)
            else Image(img.bitmap, contentDescription = label, contentScale = ContentScale.FillBounds, modifier = Modifier.fillMaxSize())
            if (probe != null && onTap != null) {
                Canvas(Modifier.fillMaxSize()) {
                    val c = Offset(probe.pixelX.toFloat() / w * size.width, probe.pixelY.toFloat() / h * size.height)
                    drawCircle(Color.White, radius = 10.dp.toPx(), center = c, style = Stroke(2.dp.toPx()))
                    drawCircle(Color.Black, radius = 12.dp.toPx(), center = c, style = Stroke(1.dp.toPx()))
                }
            }
        }
    }
}

@Composable
private fun ProbeLine(p: DepthProbe?) {
    val t = when {
        p == null -> "Toque na profundidade para medir a distância de um ponto."
        !p.valid -> "Pixel (${p.pixelX}, ${p.pixelY}): sem medida (valor bruto ${p.raw})."
        else -> "Pixel (${p.pixelX}, ${p.pixelY}): %d mm · X %.0f · Y %.0f · Z %.0f mm (bruto %d)"
            .format(p.mm, p.x, p.y, p.z, p.raw)
    }
    Text(t, style = MaterialTheme.typography.bodyMedium, fontWeight = if (p?.valid == true) FontWeight.SemiBold else FontWeight.Normal)
}

@Composable
private fun StatsLine(s: SensorsUiState) {
    val st = s.stats ?: return
    val x = st.stream
    val parts = buildList {
        add("Prof. %.1f fps (%d)".format(x.depthFps, x.depthFrames))
        if (st.videoKind != VideoKind.NONE) add("Vídeo %.1f fps (%d)".format(x.videoFps, x.videoFrames))
        x.syncDeltaMs?.let { add("Δ %.1f ms".format(it)) }
        add("%.0f%% válidos".format(x.depthValidRatio * 100))
    }
    Text(parts.joinToString(" · "), style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
}

@Composable
private fun Actions(s: SensorsUiState, vm: SensorsViewModel) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        FilledTonalButton(onClick = { vm.capture() }, enabled = !s.busy && s.source != SourceKind.NONE) {
            Text(if (s.busy) "Salvando…" else "Capturar quadro")
        }
        if (s.source == SourceKind.LIVE) {
            Button(
                onClick = { vm.toggleRecording() },
                enabled = s.streaming || s.recording,
                colors = if (s.recording) ButtonDefaults.buttonColors(containerColor = RecRed) else ButtonDefaults.buttonColors(),
            ) { Text(if (s.recording) "Parar gravação" else "Gravar") }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun PlaybackControls(s: SensorsUiState, vm: SensorsViewModel) {
    val p = s.stats?.playback
    Section("Reprodução") {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton(onClick = { vm.setPlayback(paused = !s.playbackPaused) }) { Text(if (s.playbackPaused) "Continuar" else "Pausar") }
            Spacer(Modifier.width(12.dp))
            Text("Repetir")
            Spacer(Modifier.width(6.dp))
            Switch(checked = s.playbackLoop, onCheckedChange = { vm.setPlayback(loop = it) })
        }
        if (p != null) {
            Slider(value = p.position.toFloat(), onValueChange = { vm.seek(it) })
            Text("%s / %s".format(StorageFormat.formatDuration(p.position * p.durationSeconds), StorageFormat.formatDuration(p.durationSeconds)),
                style = MaterialTheme.typography.bodySmall)
            if (p.truncated) Text("A gravação foi interrompida antes do fim; reproduzindo até o último quadro completo.",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(0.25, 0.5, 1.0, 2.0).forEach { sp ->
                FilterChip(selected = s.playbackSpeed == sp, onClick = { vm.setPlayback(speed = sp) }, label = { Text("${sp}×") })
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun DepthControls(s: SensorsUiState, vm: SensorsViewModel) {
    val d = s.depth
    Section("Profundidade") {
        Text("Faixa exibida: ${d.minMm} – ${d.maxMm} mm", style = MaterialTheme.typography.bodySmall)
        RangeSlider(
            value = d.minMm.toFloat()..d.maxMm.toFloat(),
            onValueChange = { r -> vm.setDepthParams(d.copy(minMm = (r.start / 50).toInt() * 50, maxMm = (r.endInclusive / 50).toInt() * 50)) },
            valueRange = 400f..8000f,
        )
        Text("Mapa de cor", style = MaterialTheme.typography.labelMedium)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            FilterChip(selected = d.colormap == NativeKinect.COLORMAP_TURBO, onClick = { vm.setDepthParams(d.copy(colormap = NativeKinect.COLORMAP_TURBO)) }, label = { Text("Turbo") })
            FilterChip(selected = d.colormap == NativeKinect.COLORMAP_GRAY, onClick = { vm.setDepthParams(d.copy(colormap = NativeKinect.COLORMAP_GRAY)) }, label = { Text("Cinza") })
        }
        Text("Filtro de ruído", style = MaterialTheme.typography.labelMedium)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(NativeKinect.FILTER_NONE to "Nenhum", NativeKinect.FILTER_MEDIAN to "Mediana", NativeKinect.FILTER_EDGES to "Remover bordas",
                NativeKinect.FILTER_MEDIAN_EDGES to "Mediana + bordas").forEach { (f, label) ->
                FilterChip(selected = d.filter == f, onClick = { vm.setDepthParams(d.copy(filter = f)) }, label = { Text(label) })
            }
        }
        Text("Preto: sem medida. Cinza escuro: fora da faixa. Os filtros nunca preenchem buracos e não alteram o arquivo bruto salvo.",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun VideoControls(s: SensorsUiState, vm: SensorsViewModel) {
    val v = s.video
    Section("Vídeo (somente visualização)") {
        Text("Brilho %+.2f".format(v.brightness), style = MaterialTheme.typography.bodySmall)
        Slider(value = v.brightness, onValueChange = { vm.setVideoParams(v.copy(brightness = it)) }, valueRange = -0.5f..0.5f)
        Text("Contraste %.2f×".format(v.contrast), style = MaterialTheme.typography.bodySmall)
        Slider(value = v.contrast, onValueChange = { vm.setVideoParams(v.copy(contrast = it)) }, valueRange = 0.5f..4f)
        Row(verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = { vm.setVideoParams(VideoViewParams()) }) { Text("Restaurar") }
            Text("Capturas e gravações guardam a imagem original, sem estes ajustes.",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun InfoCard(s: SensorsUiState) {
    val st = s.stats
    Section("Informações") {
        Kv("Profundidade", "640×480, 11 bits brutos → mm pela calibração")
        if (st != null && st.videoKind != VideoKind.NONE) {
            Kv(if (st.videoKind == VideoKind.RGB) "RGB" else "Infravermelho", "${st.videoWidth}×${st.videoHeight}, " + if (st.videoKind == VideoKind.RGB) "24 bits" else "8 bits")
        }
        if (st != null) {
            Kv("Faixa medida", if (st.stream.depthMaxMm > 0) "%.0f – %.0f mm".format(st.stream.depthMinMm, st.stream.depthMaxMm) else "—")
            Kv("Distância no centro", if (st.stream.depthCenterMm > 0) "%.0f mm".format(st.stream.depthCenterMm) else "sem medida")
        }
        Kv("Espaço livre", StorageFormat.formatBytes(s.freeBytes))
        if (s.source == SourceKind.LIVE) {
            val rate = StorageFormat.bytesPerSecond(s.mode.selection.depth, s.mode.selection.video?.name)
            Kv("Gravação", "~%.0f MB/s · cabe ~%.0f min".format(rate / 1e6, StorageFormat.minutesAvailable(s.freeBytes, rate)))
        }
    }
}

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    Card(colors = CardDefaults.cardColors()) {
        Column(Modifier.padding(12.dp).fillMaxWidth()) {
            Text(title, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(6.dp))
            content()
        }
    }
}

@Composable
private fun Kv(k: String, v: String) {
    Row(Modifier.padding(vertical = 2.dp)) {
        Text(k, style = MaterialTheme.typography.bodySmall, modifier = Modifier.width(130.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(v, style = MaterialTheme.typography.bodySmall)
    }
}
