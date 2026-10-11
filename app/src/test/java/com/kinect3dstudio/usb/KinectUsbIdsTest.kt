package com.kinect3dstudio.usb

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class KinectUsbIdsTest {
    @Test fun identificaSubdispositivosDoKinectV1() {
        assertEquals(KinectUsbIds.Role.CAMERA, KinectUsbIds.identify(0x045E, 0x02AE)!!.role)
        assertEquals(KinectUsbIds.Role.MOTOR, KinectUsbIds.identify(0x045E, 0x02B0)!!.role)
        assertEquals(KinectUsbIds.Role.AUDIO, KinectUsbIds.identify(0x045E, 0x02AD)!!.role)
        assertEquals(KinectUsbIds.Family.KINECT_V1_WINDOWS, KinectUsbIds.identify(0x045E, 0x02BF)!!.family)
    }

    @Test fun naoConfundeOutrasGeracoes() {
        assertEquals(KinectUsbIds.Family.KINECT_V2, KinectUsbIds.identify(0x045E, 0x02D8)!!.family)
        assertEquals(KinectUsbIds.Family.AZURE_KINECT, KinectUsbIds.identify(0x045E, 0x097C)!!.family)
        assertNotNull(KinectUsbIds.unsupportedReason(KinectUsbIds.Family.KINECT_V2))
        assertNotNull(KinectUsbIds.unsupportedReason(KinectUsbIds.Family.AZURE_KINECT))
        assertNull(KinectUsbIds.unsupportedReason(KinectUsbIds.Family.KINECT_V1_XBOX360))
    }

    @Test fun ignoraOutrosFabricantesEProdutos() {
        assertNull(KinectUsbIds.identify(0x1234, 0x02AE))
        assertNull(KinectUsbIds.identify(0x045E, 0x0001))
    }

    @Test fun nomeDoModeloPeloBcdDevice() {
        assertEquals(true, KinectUsbIds.v1ModelName(0x02AE, 0x010B).contains("1414"))
        assertEquals(true, KinectUsbIds.v1ModelName(0x02AE, 0x010F).contains("1473"))
        assertEquals(true, KinectUsbIds.v1ModelName(0x02BF, 0x0100).contains("Windows"))
    }
}
