package com.kinect3dstudio.usb

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume

/** Eventos de conexão física vindos do sistema. */
sealed interface UsbEvent {
    val device: UsbDevice
    data class Attached(override val device: UsbDevice) : UsbEvent
    data class Detached(override val device: UsbDevice) : UsbEvent
}

/**
 * Encapsula UsbManager: detecção de USB Host, listagem, permissão e abertura.
 */
class UsbAccess(private val context: Context) {
    private val app = context.applicationContext
    private val manager: UsbManager? = app.getSystemService(UsbManager::class.java)

    /** O aparelho declara suporte a USB Host (OTG)? */
    val hostSupported: Boolean
        get() = app.packageManager.hasSystemFeature(PackageManager.FEATURE_USB_HOST) && manager != null

    fun devices(): List<UsbDevice> = manager?.deviceList?.values?.toList().orEmpty()

    fun summaries(): List<UsbDeviceSummary> =
        devices().map { UsbDeviceSummary(it.deviceName, it.vendorId, it.productId) }

    fun find(summary: UsbDeviceSummary): UsbDevice? = devices().firstOrNull { it.deviceName == summary.deviceName }

    fun hasPermission(device: UsbDevice) = manager?.hasPermission(device) == true

    /** Mostra o diálogo do Android e suspende até a resposta do usuário. */
    suspend fun requestPermission(device: UsbDevice): Boolean {
        val mgr = manager ?: return false
        if (mgr.hasPermission(device)) return true
        return suspendCancellableCoroutine { cont ->
            val action = "${app.packageName}.USB_PERMISSION.${device.deviceId}"
            val receiver = object : BroadcastReceiver() {
                override fun onReceive(c: Context, intent: Intent) {
                    if (intent.action != action) return
                    runCatching { app.unregisterReceiver(this) }
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    if (cont.isActive) cont.resume(granted && mgr.hasPermission(device))
                }
            }
            ContextCompat.registerReceiver(app, receiver, IntentFilter(action), ContextCompat.RECEIVER_NOT_EXPORTED)
            // Android 14+: PendingIntent mutável precisa de Intent explícita (setPackage).
            val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
            val pi = PendingIntent.getBroadcast(app, device.deviceId, Intent(action).setPackage(app.packageName), flags)
            mgr.requestPermission(device, pi)
            cont.invokeOnCancellation { runCatching { app.unregisterReceiver(receiver) } }
        }
    }

    fun open(device: UsbDevice): UsbDeviceConnection? = manager?.openDevice(device)

    /** Conexões e desconexões físicas enquanto o fluxo é coletado. */
    fun events(): Flow<UsbEvent> = callbackFlow {
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) {
                val dev = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    ?: return
                when (intent.action) {
                    UsbManager.ACTION_USB_DEVICE_ATTACHED -> trySend(UsbEvent.Attached(dev))
                    UsbManager.ACTION_USB_DEVICE_DETACHED -> trySend(UsbEvent.Detached(dev))
                }
            }
        }
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        // Broadcasts protegidos do sistema chegam mesmo com NOT_EXPORTED.
        ContextCompat.registerReceiver(app, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
        awaitClose { runCatching { app.unregisterReceiver(receiver) } }
    }

    /** Endpoints declarados pelo dispositivo (disponível sem permissão). */
    fun endpointsOf(device: UsbDevice): List<EndpointInfo> = buildList {
        for (i in 0 until device.interfaceCount) {
            val iface = device.getInterface(i)
            for (e in 0 until iface.endpointCount) {
                val ep = iface.getEndpoint(e)
                add(
                    EndpointInfo(
                        interfaceNumber = iface.id,
                        alternateSetting = iface.alternateSetting,
                        address = ep.address,
                        transferType = ep.type,
                        maxPacketSizeRaw = ep.maxPacketSize,
                        interval = ep.interval,
                    ),
                )
            }
        }
    }
}
