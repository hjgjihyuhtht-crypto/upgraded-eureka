package com.kinect3dstudio.device

/**
 * Avalia um trecho de transmissão real e traduz os números em diagnóstico.
 * Código puro, testado em StreamSelfTestTest.
 */
object StreamSelfTest {
    const val NOMINAL_FPS = 30.0
    const val MIN_ACCEPTABLE_FPS = 20.0

    enum class Verdict { OK, DEGRADED, FAILED }

    data class PhaseResult(
        val label: String,
        val verdict: Verdict,
        val findings: List<String>,
        val stats: StreamStats,
    )

    fun evaluate(label: String, selection: StreamSelection, stats: StreamStats, seconds: Double): PhaseResult {
        val findings = mutableListOf<String>()
        var verdict = Verdict.OK
        fun worsen(v: Verdict) { if (v.ordinal > verdict.ordinal) verdict = v }

        if (stats.deviceLost) {
            findings += "O Kinect saiu do barramento durante o teste. Verifique o cabo OTG e a fonte de 12 V."
            worsen(Verdict.FAILED)
        }

        fun checkStream(name: String, frames: Long, fps: Double) {
            val avg = if (seconds > 0) frames / seconds else 0.0
            when {
                frames == 0L -> {
                    findings += "$name: nenhum quadro recebido. O controlador USB do celular pode não suportar " +
                        "transferências isócronas de alta largura de banda (USB 2.0 high-speed), ou um hub/adaptador " +
                        "está limitando a velocidade."
                    worsen(Verdict.FAILED)
                }
                maxOf(fps, avg) < MIN_ACCEPTABLE_FPS -> {
                    findings += "$name: %.1f quadros/s (esperado ~%.0f). Há perda de quadros por banda USB ou CPU."
                        .format(maxOf(fps, avg), NOMINAL_FPS)
                    worsen(Verdict.DEGRADED)
                }
                else -> findings += "$name: %.1f quadros/s, %d quadros recebidos.".format(maxOf(fps, avg), frames)
            }
        }

        if (selection.depth) {
            checkStream("Profundidade", stats.depthFrames, stats.depthFps)
            if (stats.depthFrames > 0) {
                val pct = stats.depthValidRatio * 100
                if (stats.depthValidRatio < 0.05) {
                    findings += "Profundidade: só %.1f%% dos pixels têm distância. O sensor pode estar tampado, ".format(pct) +
                        "o alvo fora da faixa (~0,5 a 4 m) ou sob luz solar direta."
                    worsen(Verdict.DEGRADED)
                } else {
                    findings += "Profundidade: %.0f%% dos pixels válidos, de %.0f a %.0f mm."
                        .format(pct, stats.depthMinMm, stats.depthMaxMm)
                }
                if (stats.depthCenterMm > 0) {
                    findings += "Distância medida no centro da imagem: %.0f mm.".format(stats.depthCenterMm)
                }
            }
        }
        selection.video?.let { v ->
            val name = if (v == SensorStream.IR) "Infravermelho" else "RGB"
            checkStream(name, stats.videoFrames, stats.videoFps)
            if (stats.videoFrames > 0) {
                findings += "$name: intensidade média %.0f de 255.".format(stats.videoMeanIntensity)
            }
        }
        stats.syncDeltaMs?.let {
            findings += "Defasagem entre os últimos quadros de profundidade e vídeo: %.1f ms.".format(it)
        }
        return PhaseResult(label, verdict, findings, stats)
    }
}
