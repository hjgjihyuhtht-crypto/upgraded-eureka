package com.kinect3dstudio.usb

/**
 * Descrição neutra de um endpoint (preenchida a partir de UsbEndpoint ou do
 * descritor bruto), para que o cálculo seja testável sem Android.
 */
data class EndpointInfo(
    val interfaceNumber: Int,
    val alternateSetting: Int,
    val address: Int,
    /** bmAttributes & 0x03: 0 controle, 1 isócrono, 2 bulk, 3 interrupção. */
    val transferType: Int,
    /** wMaxPacketSize bruto, incluindo os bits 11-12 de transações extras. */
    val maxPacketSizeRaw: Int,
    val interval: Int,
) {
    val isIsochronous get() = transferType == 1
    val isIn get() = address and 0x80 != 0
    val basePacketSize get() = maxPacketSizeRaw and 0x7FF
    /** Transações por microframe em USB 2.0 high-speed (1 a 3). */
    val transactionsPerMicroframe get() = ((maxPacketSizeRaw shr 11) and 0x3) + 1
    val bytesPerMicroframe get() = basePacketSize * transactionsPerMicroframe
}

data class BandwidthEstimate(
    val isoEndpoints: List<EndpointInfo>,
    val bytesPerSecond: Double,
    /** Fração do orçamento periódico do USB 2.0 HS (80% de 60 MB/s = 48 MB/s). */
    val periodicBudgetShare: Double,
) {
    val megabytesPerSecond get() = bytesPerSecond / 1_000_000.0
}

object UsbBandwidth {
    /** 8000 microframes/s em high-speed. */
    private const val MICROFRAMES_PER_SECOND = 8000.0
    /** USB 2.0 reserva até 80% de cada microframe para transferências periódicas: 6000 bytes. */
    const val HS_PERIODIC_BYTES_PER_MICROFRAME = 6000.0

    /**
     * Estima a largura de banda isócrona exigida, considerando para cada
     * endereço de endpoint a configuração alternativa mais exigente.
     */
    fun estimateHighSpeed(endpoints: List<EndpointInfo>): BandwidthEstimate {
        val iso = endpoints.filter { it.isIsochronous && it.isIn && it.basePacketSize > 0 }
            .groupBy { it.address }
            .map { (_, list) -> list.maxBy { it.bytesPerMicroframe } }
        var bytesPerMicroframe = 0.0
        for (ep in iso) {
            // Em HS, bInterval é expoente: período = 2^(bInterval-1) microframes.
            val period = 1 shl (ep.interval.coerceIn(1, 16) - 1)
            bytesPerMicroframe += ep.bytesPerMicroframe.toDouble() / period
        }
        return BandwidthEstimate(
            isoEndpoints = iso,
            bytesPerSecond = bytesPerMicroframe * MICROFRAMES_PER_SECOND,
            periodicBudgetShare = bytesPerMicroframe / HS_PERIODIC_BYTES_PER_MICROFRAME,
        )
    }
}

/** Campos do descritor de dispositivo USB (18 bytes iniciais de getRawDescriptors()). */
data class DeviceDescriptor(
    val bcdUsb: Int,
    val vendorId: Int,
    val productId: Int,
    val bcdDevice: Int,
)

object UsbDescriptorParser {
    private fun u16(b: ByteArray, i: Int) = (b[i].toInt() and 0xFF) or ((b[i + 1].toInt() and 0xFF) shl 8)

    fun parseDevice(raw: ByteArray?): DeviceDescriptor? {
        if (raw == null || raw.size < 18) return null
        if ((raw[0].toInt() and 0xFF) != 18 || raw[1].toInt() != 1) return null
        return DeviceDescriptor(
            bcdUsb = u16(raw, 2),
            vendorId = u16(raw, 8),
            productId = u16(raw, 10),
            bcdDevice = u16(raw, 12),
        )
    }

    /** Percorre todos os descritores e devolve os endpoints com a interface/alt a que pertencem. */
    fun parseEndpoints(raw: ByteArray?): List<EndpointInfo> {
        if (raw == null) return emptyList()
        val out = mutableListOf<EndpointInfo>()
        var i = 0
        var iface = -1
        var alt = 0
        while (i + 1 < raw.size) {
            val len = raw[i].toInt() and 0xFF
            if (len < 2 || i + len > raw.size) break
            when (raw[i + 1].toInt() and 0xFF) {
                4 -> if (len >= 4) {
                    iface = raw[i + 2].toInt() and 0xFF
                    alt = raw[i + 3].toInt() and 0xFF
                }
                5 -> if (len >= 7) {
                    out += EndpointInfo(
                        interfaceNumber = iface,
                        alternateSetting = alt,
                        address = raw[i + 2].toInt() and 0xFF,
                        transferType = raw[i + 3].toInt() and 0x03,
                        maxPacketSizeRaw = u16(raw, i + 4),
                        interval = raw[i + 6].toInt() and 0xFF,
                    )
                }
            }
            i += len
        }
        return out
    }

    fun bcdToString(bcd: Int) = "%x.%02x".format(bcd shr 8, bcd and 0xFF)
}
