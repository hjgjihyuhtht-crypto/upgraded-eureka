package com.kinect3dstudio.device

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StreamSelfTestTest {
    private fun stats(
        depth: Long = 150, video: Long = 150, dFps: Double = 30.0, vFps: Double = 30.0,
        valid: Double = 0.8, lost: Boolean = false,
    ) = StreamStats(depth, video, dFps, vFps, valid, 600.0, 3900.0, 1800.0, 110.0, 4.0, lost)

    private val sel = StreamSelection(depth = true, video = SensorStream.RGB)

    @Test fun fluxoSaudavel() {
        val r = StreamSelfTest.evaluate("t", sel, stats(), 5.0)
        assertEquals(StreamSelfTest.Verdict.OK, r.verdict)
    }

    @Test fun semQuadrosFalha() {
        val r = StreamSelfTest.evaluate("t", sel, stats(depth = 0, dFps = 0.0), 5.0)
        assertEquals(StreamSelfTest.Verdict.FAILED, r.verdict)
        assertTrue(r.findings.any { it.contains("isócronas") })
    }

    @Test fun fpsBaixoDegrada() {
        val r = StreamSelfTest.evaluate("t", sel, stats(video = 50, vFps = 10.0), 5.0)
        assertEquals(StreamSelfTest.Verdict.DEGRADED, r.verdict)
    }

    @Test fun profundidadeSemPixelsValidosDegrada() {
        val r = StreamSelfTest.evaluate("t", sel, stats(valid = 0.01), 5.0)
        assertEquals(StreamSelfTest.Verdict.DEGRADED, r.verdict)
    }

    @Test fun desconexaoFalha() {
        val r = StreamSelfTest.evaluate("t", sel, stats(lost = true), 5.0)
        assertEquals(StreamSelfTest.Verdict.FAILED, r.verdict)
    }

    @Test(expected = IllegalArgumentException::class)
    fun selecaoInvalida() {
        StreamSelection(depth = true, video = SensorStream.DEPTH)
    }
}
