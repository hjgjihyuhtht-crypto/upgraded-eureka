@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class, androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.kinect3dstudio.ui.captures

import android.app.Application
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.kinect3dstudio.K3DApp
import com.kinect3dstudio.storage.StorageFormat
import com.kinect3dstudio.storage.StoredItem
import com.kinect3dstudio.storage.StoredKind
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

data class CapturesUiState(
    val items: List<StoredItem> = emptyList(),
    val freeBytes: Long = 0,
    val message: String? = null,
    val working: Boolean = false,
)

class CapturesViewModel(app: Application) : AndroidViewModel(app) {
    private val repo = (app as K3DApp).repo
    private val _state = MutableStateFlow(CapturesUiState())
    val state: StateFlow<CapturesUiState> = _state

    fun refresh() = viewModelScope.launch(Dispatchers.IO) {
        _state.value = _state.value.copy(items = repo.store.list(), freeBytes = repo.store.freeBytes())
    }

    fun delete(item: StoredItem) = viewModelScope.launch(Dispatchers.IO) {
        val ok = repo.store.delete(item)
        if (ok) repo.log.info("Excluído: ${item.name}") else repo.log.error("Falha ao excluir ${item.name}")
        _state.value = _state.value.copy(message = if (ok) "Excluído: ${item.name}" else "Não foi possível excluir ${item.name}")
        refresh()
    }

    fun export(item: StoredItem, tree: Uri) = viewModelScope.launch(Dispatchers.IO) {
        _state.value = _state.value.copy(working = true)
        val msg = try {
            val n = repo.store.exportTo(getApplication<Application>(), tree, item)
            repo.log.info("Exportado ${item.name}: $n arquivo(s)")
            "Exportado: ${item.name} ($n arquivo(s))"
        } catch (t: Throwable) {
            repo.log.error("Falha ao exportar ${item.name}: ${t.message}")
            "Falha ao exportar: ${t.message}"
        }
        _state.value = _state.value.copy(working = false, message = msg)
    }

    fun clearMessage() { _state.value = _state.value.copy(message = null) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CapturesScreen(vm: CapturesViewModel, onBack: () -> Unit, onPlay: (StoredItem) -> Unit) {
    val s by vm.state.collectAsStateWithLifecycle()
    val snack = remember { SnackbarHostState() }
    var pendingExport by remember { mutableStateOf<StoredItem?>(null) }
    var confirmDelete by remember { mutableStateOf<StoredItem?>(null) }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        val item = pendingExport
        pendingExport = null
        if (uri != null && item != null) vm.export(item, uri)
    }

    LaunchedEffect(Unit) { vm.refresh() }
    LaunchedEffect(s.message) { s.message?.let { snack.showSnackbar(it); vm.clearMessage() } }
    BackHandler { onBack() }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Capturas e gravações") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Voltar") } },
            )
        },
        snackbarHost = { SnackbarHost(snack) },
    ) { pad ->
        LazyColumn(
            Modifier.fillMaxSize().padding(pad),
            contentPadding = PaddingValues(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            item {
                Text(
                    "Arquivos guardados na área privada do app (${StorageFormat.formatBytes(s.freeBytes)} livres). " +
                        "Use Exportar para copiar para uma pasta do celular.",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            if (s.items.isEmpty()) item { Text("Nenhuma captura ou gravação ainda.", modifier = Modifier.padding(top = 16.dp)) }
            items(s.items, key = { it.file.absolutePath }) { item ->
                ItemCard(
                    item = item,
                    working = s.working,
                    onPlay = { onPlay(item) },
                    onExport = { pendingExport = item; picker.launch(null) },
                    onDelete = { confirmDelete = item },
                )
            }
        }
    }

    confirmDelete?.let { item ->
        AlertDialog(
            onDismissRequest = { confirmDelete = null },
            title = { Text("Excluir definitivamente?") },
            text = { Text("${item.name} (${StorageFormat.formatBytes(item.sizeBytes)}) será apagado do aparelho. Não há como desfazer.") },
            confirmButton = { TextButton(onClick = { vm.delete(item); confirmDelete = null }) { Text("Excluir") } },
            dismissButton = { TextButton(onClick = { confirmDelete = null }) { Text("Cancelar") } },
        )
    }
}

@Composable
private fun ItemCard(item: StoredItem, working: Boolean, onPlay: () -> Unit, onExport: () -> Unit, onDelete: () -> Unit) {
    val date = SimpleDateFormat("dd/MM/yyyy HH:mm:ss", Locale.forLanguageTag("pt-BR")).format(Date(item.modifiedMs))
    Card {
        Column(Modifier.padding(12.dp).fillMaxWidth()) {
            Text(if (item.kind == StoredKind.RECORDING) "Gravação" else "Captura de quadro",
                style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
            Text(item.name, fontWeight = FontWeight.SemiBold)
            Text(
                "$date · ${StorageFormat.formatBytes(item.sizeBytes)}" + if (item.kind == StoredKind.CAPTURE) " · ${item.fileCount} arquivos" else "",
                style = MaterialTheme.typography.bodySmall,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 8.dp)) {
                if (item.kind == StoredKind.RECORDING) FilledTonalButton(onClick = onPlay) { Text("Reproduzir") }
                OutlinedButton(onClick = onExport, enabled = !working) { Text(if (working) "Exportando…" else "Exportar") }
                TextButton(onClick = onDelete) { Text("Excluir") }
            }
        }
    }
}
