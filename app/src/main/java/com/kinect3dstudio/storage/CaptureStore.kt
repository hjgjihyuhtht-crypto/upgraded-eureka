package com.kinect3dstudio.storage

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

enum class StoredKind { CAPTURE, RECORDING }

data class StoredItem(
    val kind: StoredKind,
    val name: String,
    val file: File,
    val sizeBytes: Long,
    val modifiedMs: Long,
    val fileCount: Int,
)

/**
 * Armazenamento privado do app (filesDir): nenhuma outra app lê sem exportação explícita.
 *   captures/CAP_aaaaMMdd_HHmmss_SSS/  quadros capturados
 *   recordings/REC_aaaaMMdd_HHmmss.k3drec  gravações contínuas
 * Capturas são escritas em uma pasta ".tmp" e renomeadas no fim; sobras são limpas na abertura.
 */
class CaptureStore(context: Context) {
    private val root: File = context.filesDir
    val capturesDir = File(root, "captures").apply { mkdirs() }
    val recordingsDir = File(root, "recordings").apply { mkdirs() }

    private fun stamp(pattern: String) = SimpleDateFormat(pattern, Locale.ROOT).format(Date())

    fun newCaptureTempDir(): File =
        File(capturesDir, "CAP_${stamp("yyyyMMdd_HHmmss_SSS")}.tmp").apply { mkdirs() }

    fun commitCapture(tmp: File): File {
        val final = File(capturesDir, tmp.name.removeSuffix(".tmp"))
        check(tmp.renameTo(final)) { "Não foi possível finalizar ${tmp.name}" }
        return final
    }

    fun newRecordingFile(): File = File(recordingsDir, "REC_${stamp("yyyyMMdd_HHmmss")}.k3drec")

    /** Remove capturas incompletas de sessões interrompidas. Retorna quantas foram removidas. */
    fun cleanupTemporary(): Int =
        capturesDir.listFiles { f -> f.isDirectory && f.name.endsWith(".tmp") }.orEmpty()
            .count { it.deleteRecursively() }

    fun list(): List<StoredItem> {
        val caps = capturesDir.listFiles { f -> f.isDirectory && !f.name.endsWith(".tmp") }.orEmpty().map { d ->
            val files = d.listFiles().orEmpty()
            StoredItem(StoredKind.CAPTURE, d.name, d, files.sumOf { it.length() }, d.lastModified(), files.size)
        }
        val recs = recordingsDir.listFiles { f -> f.isFile && f.name.endsWith(".k3drec") }.orEmpty().map { f ->
            StoredItem(StoredKind.RECORDING, f.name, f, f.length(), f.lastModified(), 1)
        }
        return (caps + recs).sortedByDescending { it.modifiedMs }
    }

    fun delete(item: StoredItem): Boolean = item.file.deleteRecursively()

    fun freeBytes(): Long = root.usableSpace

    /**
     * Copia o item para uma pasta escolhida pelo usuário (Storage Access Framework).
     * Retorna o número de arquivos copiados.
     */
    fun exportTo(context: Context, treeUri: Uri, item: StoredItem): Int {
        val tree = DocumentFile.fromTreeUri(context, treeUri) ?: error("Pasta de destino inválida")
        val resolver = context.contentResolver
        fun copy(src: File, parent: DocumentFile): Int {
            val doc = parent.createFile("application/octet-stream", src.name)
                ?: error("Não foi possível criar ${src.name} no destino")
            resolver.openOutputStream(doc.uri)?.use { out -> src.inputStream().use { it.copyTo(out, 1 shl 20) } }
                ?: error("Sem acesso de escrita ao destino")
            return 1
        }
        return when (item.kind) {
            StoredKind.RECORDING -> copy(item.file, tree)
            StoredKind.CAPTURE -> {
                val dir = tree.createDirectory(item.name) ?: error("Não foi possível criar a pasta ${item.name}")
                item.file.listFiles().orEmpty().sumOf { copy(it, dir) }
            }
        }
    }
}
