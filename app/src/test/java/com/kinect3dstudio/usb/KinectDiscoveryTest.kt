package com.kinect3dstudio.usb

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class KinectDiscoveryTest {
    private fun dev(bus: Int, addr: Int, pid: Int, vid: Int = 0x045E) =
        UsbDeviceSummary("/dev/bus/usb/%03d/%03d".format(bus, addr), vid, pid)

    @Test fun nadaConectado() {
        assertEquals(DiscoveryResult.NothingFound, KinectDiscovery.classify(emptyList()))
        assertEquals(DiscoveryResult.NothingFound, KinectDiscovery.classify(listOf(dev(1, 2, 0x1111, vid = 0x1234))))
    }

    @Test fun kinectV1Completo() {
        val r = KinectDiscovery.classify(listOf(dev(1, 3, 0x02B0), dev(1, 4, 0x02AD), dev(1, 5, 0x02AE)))
        r as DiscoveryResult.KinectV1
        assertEquals(0x02AE, r.camera.productId)
        assertEquals(0x02B0, r.motor!!.productId)
        assertEquals(0x02AD, r.audio!!.productId)
    }

    @Test fun irmaosSoNoMesmoBarramento() {
        val r = KinectDiscovery.classify(listOf(dev(2, 3, 0x02B0), dev(1, 5, 0x02AE))) as DiscoveryResult.KinectV1
        assertNull(r.motor)
    }

    @Test fun semFonteSoMotorAparece() {
        val r = KinectDiscovery.classify(listOf(dev(1, 3, 0x02B0)))
        assertTrue(r is DiscoveryResult.V1CameraMissing)
        assertTrue((r as DiscoveryResult.V1CameraMissing).reason.contains("12 V"))
    }

    @Test fun kinectV2NaoEhTratadoComoV1() {
        val r = KinectDiscovery.classify(listOf(dev(1, 3, 0x02D8)))
        assertTrue(r is DiscoveryResult.Unsupported)
        assertEquals(KinectUsbIds.Family.KINECT_V2, (r as DiscoveryResult.Unsupported).family)
    }

    @Test fun barramentoExtraido() {
        assertEquals(1, dev(1, 5, 0x02AE).bus)
        assertEquals(-1, UsbDeviceSummary("estranho", 0, 0).bus)
    }
}
