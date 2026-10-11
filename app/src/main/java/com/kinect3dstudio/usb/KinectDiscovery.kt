package com.kinect3dstudio.usb

/** Resumo neutro de um UsbDevice (testável sem Android). */
data class UsbDeviceSummary(
    val deviceName: String,
    val vendorId: Int,
    val productId: Int,
) {
    /** Barramento extraído de "/dev/bus/usb/BBB/DDD" (−1 se o formato for outro). */
    val bus: Int get() = deviceName.split('/').let { p ->
        if (p.size >= 2) p[p.size - 2].toIntOrNull() ?: -1 else -1
    }
}

/** Resultado da detecção: o que foi encontrado e o que isso significa. */
sealed interface DiscoveryResult {
    data object NothingFound : DiscoveryResult

    /** Kinect v1 com câmera presente: pode ser aberto. */
    data class KinectV1(
        val family: KinectUsbIds.Family,
        val camera: UsbDeviceSummary,
        val motor: UsbDeviceSummary?,
        val audio: UsbDeviceSummary?,
    ) : DiscoveryResult

    /** Só apareceram motor e/ou áudio. A câmera não enumerou. */
    data class V1CameraMissing(val present: List<UsbDeviceSummary>) : DiscoveryResult {
        val reason =
            "Apenas ${present.joinToString { roleName(it) }} do Kinect apareceu no USB. " +
                "A câmera (RGB/IR/profundidade) só enumera com a fonte de 12 V ligada na tomada. " +
                "Verifique o adaptador de energia do Kinect e reconecte o cabo."
    }

    data class Unsupported(val family: KinectUsbIds.Family, val device: UsbDeviceSummary) : DiscoveryResult {
        val reason: String = KinectUsbIds.unsupportedReason(family) ?: ""
    }

    companion object {
        fun roleName(d: UsbDeviceSummary): String =
            when (KinectUsbIds.identify(d.vendorId, d.productId)?.role) {
                KinectUsbIds.Role.CAMERA -> "a câmera"
                KinectUsbIds.Role.MOTOR -> "o motor"
                KinectUsbIds.Role.AUDIO -> "o áudio"
                else -> "um dispositivo"
            }
    }
}

object KinectDiscovery {
    fun classify(devices: List<UsbDeviceSummary>): DiscoveryResult {
        val identified = devices.mapNotNull { d ->
            KinectUsbIds.identify(d.vendorId, d.productId)?.let { d to it }
        }
        if (identified.isEmpty()) return DiscoveryResult.NothingFound

        val v1 = identified.filter {
            it.second.family == KinectUsbIds.Family.KINECT_V1_XBOX360 ||
                it.second.family == KinectUsbIds.Family.KINECT_V1_WINDOWS
        }
        val camera = v1.firstOrNull { it.second.role == KinectUsbIds.Role.CAMERA }
        if (camera != null) {
            // O Kinect v1 tem um hub interno: os sub-dispositivos ficam no mesmo barramento.
            val bus = camera.first.bus
            fun sibling(role: KinectUsbIds.Role) =
                v1.firstOrNull { it.second.role == role && it.first.bus == bus }?.first
            return DiscoveryResult.KinectV1(
                family = camera.second.family,
                camera = camera.first,
                motor = sibling(KinectUsbIds.Role.MOTOR),
                audio = sibling(KinectUsbIds.Role.AUDIO),
            )
        }
        if (v1.isNotEmpty()) return DiscoveryResult.V1CameraMissing(v1.map { it.first })

        val other = identified.first()
        return DiscoveryResult.Unsupported(other.second.family, other.first)
    }
}
