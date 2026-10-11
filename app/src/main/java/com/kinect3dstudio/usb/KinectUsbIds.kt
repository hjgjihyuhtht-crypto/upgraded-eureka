package com.kinect3dstudio.usb

/**
 * Catálogo de VID/PID conhecidos da família Kinect e classificação de cada
 * sub-dispositivo. Código puro (sem Android), testado em KinectUsbIdsTest.
 *
 * Fontes: tabelas de PID do libfreenect (freenect_internal.h) e libfreenect2.
 */
object KinectUsbIds {
    const val VID_MICROSOFT = 0x045E

    // Kinect v1 (Xbox 360, modelos 1414 e 1473)
    const val PID_NUI_CAMERA = 0x02AE
    const val PID_NUI_MOTOR = 0x02B0
    const val PID_NUI_AUDIO = 0x02AD

    // Kinect for Windows v1
    const val PID_K4W_CAMERA = 0x02BF
    const val PID_K4W_AUDIO = 0x02BE
    const val PID_K4W_AUDIO_ALT_1 = 0x02C3
    const val PID_K4W_AUDIO_ALT_2 = 0x02BB

    // Kinect v2 (Xbox One / Kinect for Windows v2)
    const val PID_KV2_A = 0x02C4
    const val PID_KV2_B = 0x02D8
    const val PID_KV2_C = 0x02D9

    // Azure Kinect DK (hub interno com câmeras e microfones)
    val AZURE_PIDS = setOf(0x097A, 0x097B, 0x097C, 0x097D, 0x097E)

    /** Bcd do dispositivo da câmera do modelo 1414 (o "original", com motor próprio). */
    const val BCD_MODEL_1414 = 0x010B

    enum class Role { CAMERA, MOTOR, AUDIO, UNKNOWN }

    enum class Family(val displayName: String) {
        KINECT_V1_XBOX360("Kinect v1 (Xbox 360)"),
        KINECT_V1_WINDOWS("Kinect for Windows v1"),
        KINECT_V2("Kinect v2 (Xbox One)"),
        AZURE_KINECT("Azure Kinect DK"),
    }

    data class Identified(val family: Family, val role: Role)

    fun identify(vendorId: Int, productId: Int): Identified? {
        if (vendorId != VID_MICROSOFT) return null
        return when (productId) {
            PID_NUI_CAMERA -> Identified(Family.KINECT_V1_XBOX360, Role.CAMERA)
            PID_NUI_MOTOR -> Identified(Family.KINECT_V1_XBOX360, Role.MOTOR)
            PID_NUI_AUDIO -> Identified(Family.KINECT_V1_XBOX360, Role.AUDIO)
            PID_K4W_CAMERA -> Identified(Family.KINECT_V1_WINDOWS, Role.CAMERA)
            PID_K4W_AUDIO, PID_K4W_AUDIO_ALT_1, PID_K4W_AUDIO_ALT_2 ->
                Identified(Family.KINECT_V1_WINDOWS, Role.AUDIO)
            PID_KV2_A, PID_KV2_B, PID_KV2_C -> Identified(Family.KINECT_V2, Role.CAMERA)
            in AZURE_PIDS -> Identified(Family.AZURE_KINECT, Role.UNKNOWN)
            else -> null
        }
    }

    /** Motivo técnico pelo qual uma família não é suportada por este app, ou null se é suportada. */
    fun unsupportedReason(family: Family): String? = when (family) {
        Family.KINECT_V1_XBOX360, Family.KINECT_V1_WINDOWS -> null
        Family.KINECT_V2 ->
            "Kinect v2 usa outro protocolo (USB 3.0, libfreenect2) e não é suportado nesta versão. " +
                "A API do Kinect v1 não pode controlá-lo."
        Family.AZURE_KINECT ->
            "Azure Kinect DK depende do \"depth engine\" fechado da Microsoft, distribuído só para " +
                "Windows e Linux (glibc). Ele não roda no Android, então não há profundidade disponível."
    }

    /** Nome do modelo físico do Kinect v1 a partir do bcdDevice da câmera. */
    fun v1ModelName(cameraPid: Int, bcdDevice: Int): String = when {
        cameraPid == PID_K4W_CAMERA -> "Kinect for Windows v1"
        bcdDevice == BCD_MODEL_1414 -> "Kinect v1 Xbox 360 — modelo 1414"
        else -> "Kinect v1 Xbox 360 — modelo 1473 (bcdDevice 0x%04X)".format(bcdDevice)
    }

    fun hex(id: Int) = "0x%04X".format(id)
}
