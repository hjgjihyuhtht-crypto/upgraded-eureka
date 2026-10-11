package com.kinect3dstudio.ui

import android.content.Intent
import android.hardware.usb.UsbManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.lifecycle.lifecycleScope
import com.kinect3dstudio.game.GameActivity
import kotlinx.coroutines.launch
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import com.kinect3dstudio.ui.captures.CapturesScreen
import com.kinect3dstudio.ui.captures.CapturesViewModel
import com.kinect3dstudio.ui.sensors.SensorsEntry
import com.kinect3dstudio.ui.sensors.SensorsScreen
import com.kinect3dstudio.ui.sensors.SensorsViewModel
import com.kinect3dstudio.ui.theme.K3DTheme

private sealed interface Screen {
    data object Home : Screen
    data class Sensors(val entry: SensorsEntry) : Screen
    data object Captures : Screen
}

class MainActivity : ComponentActivity() {
    private val vm: MainViewModel by viewModels()
    private val sensorsVm: SensorsViewModel by viewModels()
    private val capturesVm: CapturesViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            K3DTheme {
                var screen by remember { mutableStateOf<Screen>(Screen.Home) }
                when (val sc = screen) {
                    Screen.Home -> HomeScreen(
                        vm,
                        onOpenSensors = { screen = Screen.Sensors(SensorsEntry.Live) },
                        onOpenCaptures = { screen = Screen.Captures },
                        onPlay = ::launchGame,
                    )
                    is Screen.Sensors -> SensorsScreen(
                        sensorsVm, sc.entry,
                        onBack = { screen = if (sc.entry is SensorsEntry.Playback) Screen.Captures else Screen.Home },
                    )
                    Screen.Captures -> CapturesScreen(
                        capturesVm,
                        onBack = { screen = Screen.Home },
                        onPlay = { item -> screen = Screen.Sensors(SensorsEntry.Playback(item.file.absolutePath)) },
                    )
                }
            }
        }
        if (savedInstanceState == null) handleUsbIntent(intent)
    }

    private var gameLaunched = false

    /** Solta o Kinect neste processo e abre o jogo (processo ":game"). */
    private fun launchGame(simulation: Boolean) {
        lifecycleScope.launch {
            vm.releaseForGame()
            gameLaunched = true
            startActivity(GameActivity.intent(this@MainActivity, simulation))
        }
    }

    override fun onResume() {
        super.onResume()
        if (gameLaunched) {
            gameLaunched = false
            vm.onReturnFromGame()
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleUsbIntent(intent)
    }

    private fun handleUsbIntent(intent: Intent?) {
        if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) vm.onLaunchedByUsb()
    }
}
