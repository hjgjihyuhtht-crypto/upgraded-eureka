@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class, androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.kinect3dstudio.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.kinect3dstudio.device.StreamSelfTest
import com.kinect3dstudio.device.StreamStats
import com.kinect3dstudio.nativebridge.NativeKinect
import com.kinect3dstudio.usb.DiscoveryResult
import com.kinect3dstudio.usb.KinectUsbIds

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(vm: MainViewModel, onOpenSensors: () -> Unit, onOpenCaptures: () -> Unit, onPlay: (simulation: Boolean) -> Unit) {
    val s by vm.state.collectAsStateWithLifecycle()
    val logEntries by vm.log.entries.collectAsStateWithLifecycle()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Kinect 3D Studio") },
                actions = {
                    IconButton(onClick = vm::refresh) { Icon(Icons.Filled.Refresh, contentDescription = "Procurar dispositivos") }
                },
            )
        },
    ) { pad ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(pad),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            s.crashReport?.let { report -> item { CrashCard(report, vm) } }
            item { ConnectionCard(s, vm) }
            item { ToolsCard(s, onOpenSensors, onOpenCaptures) }
            item { GameCard(s, onPlay) }
            if (!s.hostSupported || s.nativeError != null) {
                item {
                    Notice(Icons.Filled.Warning, "Limitação do aparelho",
                        listOfNotNull(
                            if (!s.hostSupported) "Este celular não declara suporte a USB Host (OTG); o Kinect não pode ser usado nele." else null,
                            s.nativeError,
                        ).joinToString("\n"))
                }
            }
            s.sensorInfo?.let { info ->
                item {
                    Section("Sensor") {
                        Kv("Modelo", info.modelName)
                        Kv("Driver", info.driverName)
                        Kv("Fluxos", info.availableStreams.joinToString { it.name })
                        Kv("Motor/LED", if (info.hasMotor) "disponível" else "indisponível")
                        info.streamRestrictions.forEach { Small("• $it") }
                        info.notes.forEach { Small("• $it") }
                    }
                }
                item { SelfTestCard(s, vm) }
                if (info.hasMotor) item { MotorCard(s, vm) }
            }
            item { UsbCard(s) }
            item { RoadmapCard() }
            item {
                val clip = LocalClipboardManager.current
                Section("Log de diagnóstico", trailing = {
                    TextButton(onClick = { clip.setText(AnnotatedString(vm.log.export())) }) { Text("Copiar") }
                }) {
                    SelectionContainer {
                        Text(
                            logEntries.takeLast(80).joinToString("\n") { it.format() }.ifEmpty { "(vazio)" },
                            fontFamily = FontFamily.Monospace, fontSize = 11.sp, lineHeight = 14.sp,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ConnectionCard(s: UiState, vm: MainViewModel) {
    val (title, detail, icon) = when (val c = s.connection) {
        ConnectionState.Idle -> when (val d = s.discovery) {
            is DiscoveryResult.KinectV1 -> Triple("Kinect detectado", "${d.family.displayName}. Toque em Conectar para autorizar o acesso USB.", Icons.Filled.Info)
            is DiscoveryResult.V1CameraMissing -> Triple("Câmera ausente", d.reason, Icons.Filled.Warning)
            is DiscoveryResult.Unsupported -> Triple("${d.family.displayName} não suportado", d.reason, Icons.Filled.Warning)
            DiscoveryResult.NothingFound -> Triple("Nenhum Kinect conectado",
                "Ligue a fonte de 12 V do Kinect na tomada e conecte o cabo USB ao celular por um adaptador OTG.", Icons.Filled.Info)
        }
        ConnectionState.Requesting -> Triple("Aguardando permissão", "Aceite os pedidos de acesso USB do Android (câmera e motor).", Icons.Filled.Info)
        ConnectionState.Opening -> Triple("Inicializando o sensor", "Abrindo a câmera e lendo a calibração do Kinect…", Icons.Filled.Info)
        ConnectionState.Connected -> Triple("Conectado", s.sensorInfo?.modelName ?: "", Icons.Filled.CheckCircle)
        is ConnectionState.Failed -> Triple("Não conectado", c.reason, Icons.Filled.Warning)
    }
    val busy = s.connection is ConnectionState.Requesting || s.connection is ConnectionState.Opening
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)) {
        Column(Modifier.padding(16.dp).fillMaxWidth()) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (busy) CircularProgressIndicator(Modifier.size(22.dp), strokeWidth = 2.dp)
                else Icon(icon, contentDescription = null)
                Spacer(Modifier.width(10.dp))
                Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            }
            Spacer(Modifier.height(6.dp))
            Text(detail, style = MaterialTheme.typography.bodyMedium)
            Spacer(Modifier.height(12.dp))
            if (s.connection == ConnectionState.Connected) {
                OutlinedButton(onClick = vm::disconnect) { Text("Desconectar") }
            } else {
                Button(onClick = vm::connect, enabled = !busy && s.hostSupported && s.nativeError == null) { Text("Conectar") }
            }
        }
    }
}

@Composable
private fun ToolsCard(s: UiState, onOpenSensors: () -> Unit, onOpenCaptures: () -> Unit) {
    val connected = s.connection == ConnectionState.Connected
    Section("Ferramentas") {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = onOpenSensors, enabled = connected && !s.testRunning) { Text("Sensores ao vivo") }
            OutlinedButton(onClick = onOpenCaptures) { Text("Capturas e gravações") }
        }
        Small(
            if (connected) "RGB, profundidade e infravermelho em tempo real, com captura de quadros e gravação."
            else "Conecte o Kinect para ver os sensores. Gravações já feitas podem ser reproduzidas sem o Kinect.",
        )
    }
}

@Composable
private fun CrashCard(report: String, vm: MainViewModel) {
    val clip = LocalClipboardManager.current
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer)) {
        Column(Modifier.padding(16.dp).fillMaxWidth()) {
            Text("O app ou o jogo fechou sozinho", style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.SemiBold)
            Small("Toque em Copiar e mande este texto para corrigirmos.")
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { clip.setText(AnnotatedString(report)) }) { Text("Copiar") }
                OutlinedButton(onClick = vm::clearCrashReport) { Text("Apagar") }
            }
            Spacer(Modifier.height(8.dp))
            SelectionContainer {
                Text(report.take(4000), fontFamily = FontFamily.Monospace, fontSize = 10.sp, lineHeight = 12.sp)
            }
        }
    }
}

@Composable
private fun GameCard(s: UiState, onPlay: (Boolean) -> Unit) {
    val kinectPresent = s.discovery is DiscoveryResult.KinectV1 || s.connection == ConnectionState.Connected
    Section("Jogar — Expedição Ativa") {
        Small(
            "Cinco jogos de movimento com o corpo inteiro (Corredeira, Rebote, Esquiva, Aquário e Gravidade Zero), " +
                "aventura com medalhas, fotos e emblemas. Fique de 2 a 3 m do Kinect, com o celular na horizontal " +
                "(de preferência ligado a uma TV ou apoiado longe de você).",
        )
        Spacer(Modifier.height(8.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = { onPlay(false) }, enabled = kinectPresent && !s.testRunning) { Text("Jogar com o Kinect") }
            OutlinedButton(onClick = { onPlay(true) }) { Text("Jogar sem Kinect (simulação)") }
        }
        if (!kinectPresent) Small("Conecte o Kinect (OTG + fonte de 12 V) para jogar com o corpo. A simulação usa botões na tela.")
    }
}

@Composable
private fun SelfTestCard(s: UiState, vm: MainViewModel) {
    Section("Teste de inicialização dos fluxos") {
        Small("Transmite dados reais por 5 s em cada fase e mede quadros, FPS e distâncias. Aponte o Kinect para uma pessoa ou parede a 1–3 m.")
        Spacer(Modifier.height(8.dp))
        if (s.testRunning) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(8.dp))
                Text("Testando: ${s.testPhase ?: ""}")
            }
            s.liveStats?.let { LiveStats(it) }
        } else {
            FilledTonalButton(onClick = { vm.runSelfTest() }) { Text("Testar fluxos") }
        }
        s.testResults.forEach { r ->
            HorizontalDivider(Modifier.padding(vertical = 8.dp))
            val verdict = when (r.verdict) {
                StreamSelfTest.Verdict.OK -> "OK"
                StreamSelfTest.Verdict.DEGRADED -> "com perdas"
                StreamSelfTest.Verdict.FAILED -> "falhou"
            }
            Text("${r.label}: $verdict", fontWeight = FontWeight.SemiBold)
            r.findings.forEach { Small("• $it") }
        }
    }
}

@Composable
private fun LiveStats(st: StreamStats) {
    Column(Modifier.padding(top = 6.dp)) {
        Kv("Profundidade", "%d quadros · %.1f fps · %.0f%% válidos".format(st.depthFrames, st.depthFps, st.depthValidRatio * 100))
        Kv("Centro", if (st.depthCenterMm > 0) "%.0f mm".format(st.depthCenterMm) else "sem medida")
        Kv("Vídeo", "%d quadros · %.1f fps".format(st.videoFrames, st.videoFps))
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun MotorCard(s: UiState, vm: MainViewModel) {
    var tilt by remember { mutableFloatStateOf(0f) }
    Section("Motor, LED e acelerômetro") {
        Small("Comandos reais enviados ao motor do Kinect (modelo 1414).")
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { vm.setLed(NativeKinect.LED_GREEN) }) { Text("LED verde") }
            OutlinedButton(onClick = { vm.setLed(NativeKinect.LED_RED) }) { Text("LED vermelho") }
            OutlinedButton(onClick = { vm.setLed(NativeKinect.LED_OFF) }) { Text("LED desligado") }
        }
        Spacer(Modifier.height(8.dp))
        Text("Inclinação: %.0f°".format(tilt))
        Slider(value = tilt, onValueChange = { tilt = it }, valueRange = -27f..27f, steps = 53)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilledTonalButton(onClick = { vm.applyTilt(tilt.toDouble()) }) { Text("Aplicar") }
            OutlinedButton(onClick = { vm.readMotor() }) { Text("Ler sensores") }
        }
        s.motor?.let {
            Kv("Ângulo lido", "%.1f°".format(it.tiltDegrees))
            Kv("Aceleração", "x %.2f · y %.2f · z %.2f m/s²".format(it.accelX, it.accelY, it.accelZ))
        }
        s.motorMessage?.let { Small(it) }
    }
}

@Composable
private fun UsbCard(s: UiState) {
    Section("Diagnóstico USB") {
        if (s.usbDevices.isEmpty()) Small("Nenhum dispositivo USB conectado ao celular.")
        s.usbDevices.forEach { d ->
            Text("${KinectUsbIds.hex(d.vid)}:${KinectUsbIds.hex(d.pid)} — ${d.label}", fontWeight = FontWeight.Medium, fontSize = 14.sp)
            Small("${d.name} · ${d.endpoints}")
            Spacer(Modifier.height(4.dp))
        }
        s.bandwidth?.let { b ->
            HorizontalDivider(Modifier.padding(vertical = 8.dp))
            Kv("Banda isócrona", "%.1f MB/s".format(b.megabytesPerSecond))
            Kv("Uso do USB 2.0", "%.0f%% do limite periódico".format(b.periodicBudgetShare * 100))
            Small(
                "O Kinect v1 precisa de USB 2.0 high-speed com transferências isócronas. Evite hubs com outros " +
                    "dispositivos ativos; nem todo controlador USB de celular entrega essa banda. O teste de fluxos confirma na prática.",
            )
        }
    }
}

@Composable
private fun RoadmapCard() {
    Section("Próximas etapas") {
        Small(
            "Etapa 2: detecção, permissão, inicialização e teste dos sensores.\n" +
                "Etapa 3: RGB, profundidade e infravermelho ao vivo, captura, gravação e reprodução.\n" +
                "Jogo Expedição Ativa (esta versão): rastreador corporal e os 5 jogos da versão de PC.\n" +
                "Etapa 4: nuvem de pontos 3D com visualizador OpenGL ES.\n" +
                "Etapa 5: scanner corporal (fusão TSDF, malha e textura).\n" +
                "Depois: motion capture, avatar, exportação e galeria.",
        )
    }
}

@Composable
private fun Section(title: String, trailing: (@Composable () -> Unit)? = null, content: @Composable () -> Unit) {
    Card {
        Column(Modifier.padding(16.dp).fillMaxWidth()) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                trailing?.invoke()
            }
            Spacer(Modifier.height(8.dp))
            content()
        }
    }
}

@Composable
private fun Notice(icon: ImageVector, title: String, body: String) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer)) {
        Row(Modifier.padding(16.dp)) {
            Icon(icon, contentDescription = null)
            Spacer(Modifier.width(10.dp))
            Column {
                Text(title, fontWeight = FontWeight.SemiBold)
                Text(body, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun Kv(k: String, v: String) {
    Row(Modifier.padding(vertical = 2.dp)) {
        Text(k, style = MaterialTheme.typography.bodySmall, modifier = Modifier.width(120.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(v, style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun Small(t: String) = Text(t, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(vertical = 1.dp))
