package com.kinect3dstudio.storage

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StorageFormatTest {
    @Test fun taxaDeGravacao() {
        // profundidade 614400 + RGB 921600 + 2 cabeçalhos de 28 bytes, a 30 Hz
        assertEquals((614_400 + 921_600 + 56) * 30.0, StorageFormat.bytesPerSecond(true, "RGB"), 0.0)
        assertEquals((614_400 + 312_320 + 56) * 30.0, StorageFormat.bytesPerSecond(true, "IR"), 0.0)
        assertEquals((614_400 + 28) * 30.0, StorageFormat.bytesPerSecond(true, null), 0.0)
    }

    @Test fun minutosDisponiveisRespeitamReserva() {
        val rate = StorageFormat.bytesPerSecond(true, "RGB")
        assertEquals(0.0, StorageFormat.minutesAvailable(StorageFormat.MIN_FREE_TO_CONTINUE, rate), 0.0)
        val oneMinute = StorageFormat.MIN_FREE_TO_CONTINUE + (rate * 60).toLong()
        assertEquals(1.0, StorageFormat.minutesAvailable(oneMinute, rate), 1e-6)
        assertTrue(StorageFormat.minutesAvailable(1000, 0.0).isInfinite())
    }

    @Test fun formatacao() {
        assertEquals("512 B", StorageFormat.formatBytes(512))
        assertTrue(StorageFormat.formatBytes(3L shl 30).startsWith("3"))
        assertTrue(StorageFormat.formatBytes(3L shl 30).endsWith("GB"))
        assertEquals("1:05", StorageFormat.formatDuration(65.9))
        assertEquals("0:00", StorageFormat.formatDuration(-3.0))
    }
}
