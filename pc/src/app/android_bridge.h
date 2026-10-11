#pragma once
// Ponte entre o jogo (raylib, NativeActivity) e o lado Kotlin do app Android.
// Implementação: app/src/main/cpp/game/game_bridge.cpp (só existe no build Android).
//
// O Kotlin (GameActivity, processo ":game") abre o Kinect com o UsbManager e entrega a fonte de
// quadros pronta; o jogo só lê. Quem abre e fecha o dispositivo é sempre o Kotlin.
#include <cstdint>
#include <string>

namespace k3d {
class FrameSource;

namespace android {

enum class SourceState { Waiting, Ready, Failed, Simulation };

struct SourceInfo {
    SourceState state = SourceState::Waiting;
    FrameSource *src = nullptr;   // válido enquanto state == Ready (dono: Kotlin)
    std::string model;            // "Kinect para Xbox 360 (1414)" etc.
    std::string message;          // andamento ou motivo da falha
    uint64_t version = 0;         // muda a cada atualização vinda do Kotlin
};

SourceInfo source();
// O jogo passa a usar a fonte da versão dada (false se ela já mudou) e depois a devolve.
// O Kotlin só fecha o Kinect depois que o jogo devolve (ou após um tempo-limite se o jogo estiver parado).
bool acquire(uint64_t version);
void release();
// Pede ao Kotlin para tentar abrir o Kinect de novo (o Kotlin observa o pedido).
void requestRetry();
// Pastas do app: interna (perfil) e externa do app (fotos), via NativeActivity.
std::string internalDir();
std::string externalDir();
// Log do jogo (raylib + etapas) num arquivo que o app mostra se o jogo fechar sozinho.
void startLog();

} // namespace android
} // namespace k3d
