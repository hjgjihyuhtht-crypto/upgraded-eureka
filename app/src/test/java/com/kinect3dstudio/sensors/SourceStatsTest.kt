package com.kinect3dstudio.sensors

import com.kinect3dstudio.nativebridge.NativeKinect as N
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SourceStatsTest {
    private fun base() = DoubleArray(N.S_COUNT).apply {
        this[N.S_DEPTH_FRAMES] = 300.0
        this[N.S_VIDEO_FRAMES] = 299.0
        this[N.S_DEPTH_FPS] = 30.0
        this[N.S_SYNC_DELTA_MS] = -1.0
        this[N.S_VIDEO_W] = 640.0
        this[N.S_VIDEO_H] = 488.0
        this[N.S_VIDEO_FORMAT] = 2.0
        this[N.S_REC_ACTIVE] = 1.0
        this[N.S_REC_FRAMES] = 120.0
        this[N.S_REC_BYTES] = 1e8
        this[N.S_PLAY_POSITION] = 0.5
        this[N.S_PLAY_DURATION_S] = 12.0
    }

    @Test fun converteVetorNativo() {
        val s = SourceStats.fromArray(base(), isPlayback = false)
        assertEquals(300L, s.stream.depthFrames)
        assertEquals(VideoKind.IR, s.videoKind)
        assertEquals(488, s.videoHeight)
        assertNull(s.stream.syncDeltaMs)
        assertTrue(s.recording.active)
        assertEquals(100_000_000L, s.recording.bytesWritten)
        assertNull(s.playback)
    }

    @Test fun reproducaoTemPosicao() {
        val s = SourceStats.fromArray(base(), isPlayback = true)
        assertEquals(0.5, s.playback!!.position, 0.0)
        assertEquals(12.0, s.playback!!.durationSeconds, 0.0)
        assertFalse(s.playback!!.truncated)
    }

    @Test(expected = IllegalArgumentException::class)
    fun vetorCurtoFalha() { SourceStats.fromArray(DoubleArray(13), false) }

    @Test fun sonda() {
        val p = DepthProbe.from(10, 20, doubleArrayOf(1.0, 700.0, 1234.0, -50.0, 20.0, 1234.0))
        assertTrue(p.valid)
        assertEquals(1234, p.mm)
        assertEquals(700, p.raw)
    }
}
