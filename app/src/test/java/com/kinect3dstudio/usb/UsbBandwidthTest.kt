package com.kinect3dstudio.usb

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class UsbBandwidthTest {
    // Endpoints da câmera do Kinect v1: 0x81 (vídeo) e 0x82 (profundidade),
    // isócronos, wMaxPacketSize 0x0BC0 = 2 x 960 bytes por microframe.
    private val kinectEndpoints = listOf(
        EndpointInfo(0, 0, 0x81, 1, 0x0BC0, 1),
        EndpointInfo(0, 0, 0x82, 1, 0x0BC0, 1),
    )

    @Test fun decodificaTransacoesExtras() {
        val ep = kinectEndpoints[0]
        assertEquals(960, ep.basePacketSize)
        assertEquals(2, ep.transactionsPerMicroframe)
        assertEquals(1920, ep.bytesPerMicroframe)
    }

    @Test fun bandaDoKinectV1() {
        val b = UsbBandwidth.estimateHighSpeed(kinectEndpoints)
        assertEquals(2, b.isoEndpoints.size)
        assertEquals(30.72, b.megabytesPerSecond, 0.001)
        assertEquals(0.64, b.periodicBudgetShare, 0.001)
    }

    @Test fun usaAlternativaMaisExigentePorEndereco() {
        val eps = listOf(
            EndpointInfo(0, 0, 0x81, 1, 0, 1),        // alt 0 sem banda (como no K4W)
            EndpointInfo(0, 1, 0x81, 1, 0x0BC0, 1),
        )
        assertEquals(1, UsbBandwidth.estimateHighSpeed(eps).isoEndpoints.size)
        assertEquals(15.36, UsbBandwidth.estimateHighSpeed(eps).megabytesPerSecond, 0.001)
    }

    @Test fun intervaloMaiorReduzBanda() {
        val b = UsbBandwidth.estimateHighSpeed(listOf(EndpointInfo(0, 0, 0x81, 1, 1024, 4)))
        assertEquals(1024 * 8000 / 8 / 1e6, b.megabytesPerSecond, 1e-9)
    }

    @Test fun ignoraBulkEOut() {
        val b = UsbBandwidth.estimateHighSpeed(listOf(EndpointInfo(0, 0, 0x81, 2, 512, 0), EndpointInfo(0, 0, 0x01, 1, 1024, 1)))
        assertEquals(0.0, b.bytesPerSecond, 0.0)
    }

    @Test fun parserDeDescritores() {
        val device = byteArrayOf(18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x5E, 0x04, 0xAE.toByte(), 0x02, 0x0B, 0x01, 1, 2, 3, 1)
        val config = byteArrayOf(9, 2, 32, 0, 1, 1, 0, 0x80.toByte(), 50)
        val iface = byteArrayOf(9, 4, 0, 0, 2, 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0)
        val ep1 = byteArrayOf(7, 5, 0x81.toByte(), 0x01, 0xC0.toByte(), 0x0B, 1)
        val ep2 = byteArrayOf(7, 5, 0x82.toByte(), 0x01, 0xC0.toByte(), 0x0B, 1)
        val raw = device + config + iface + ep1 + ep2

        val d = UsbDescriptorParser.parseDevice(raw)!!
        assertEquals(0x0200, d.bcdUsb)
        assertEquals(0x045E, d.vendorId)
        assertEquals(0x02AE, d.productId)
        assertEquals(0x010B, d.bcdDevice)
        assertEquals("2.00", UsbDescriptorParser.bcdToString(d.bcdUsb))

        val eps = UsbDescriptorParser.parseEndpoints(raw)
        assertEquals(kinectEndpoints, eps)
    }

    @Test fun parserRejeitaDadosInvalidos() {
        assertNull(UsbDescriptorParser.parseDevice(byteArrayOf(1, 2, 3)))
        assertEquals(emptyList<EndpointInfo>(), UsbDescriptorParser.parseEndpoints(byteArrayOf(0, 0, 0)))
    }
}
