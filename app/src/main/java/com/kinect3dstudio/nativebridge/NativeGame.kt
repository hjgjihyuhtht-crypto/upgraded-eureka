package com.kinect3dstudio.nativebridge

/**
 * Ponte com o jogo Expedição Ativa (raylib numa NativeActivity, dentro de libk3d_native.so).
 * Implementação: app/src/main/cpp/game/game_bridge.cpp. O jogo só LÊ a fonte de quadros;
 * abrir e fechar o Kinect é sempre feito pelo Kotlin.
 */
object NativeGame {
    /** Carrega as bibliotecas (mesmo carregamento de [NativeKinect]). */
    val loadError: String? get() = NativeKinect.loadError

    external fun nativeSetWaiting(message: String)
    /** [handle] é o FrameSource* da sessão ao vivo (ou 0 para falha). */
    external fun nativeSetSource(handle: Long, model: String)
    external fun nativeSetFailed(message: String)
    external fun nativeSetSimulation()
    /** Tira a fonte do jogo e espera ele soltar; depois disso o Kinect pode ser fechado. */
    external fun nativeRevokeSource(message: String, timeoutMs: Int): Boolean
    /** O jogador tocou em "Tentar de novo"? (limpa o pedido) */
    external fun nativeTakeRetryRequest(): Boolean
    /** Arquivo onde o jogo grava o log do raylib e das etapas de abertura. */
    external fun nativeSetLogPath(path: String)
}
