#pragma once
// Gerador de profundidade SINTÉTICA: desenha bonecos 3D (cápsulas), chão e
// parede como o Kinect veria. Serve para testes automáticos e para o modo
// "Simulação (sem Kinect)", sempre identificado como simulação na tela.
#include <cstdint>
#include <vector>

#include "body_types.h"

namespace k3d {

struct SimPose {
    bool present = true;
    float x = 0;          // lateral (plano de jogo, + = direita do jogador)
    float z = 2.6f;       // distância ao sensor
    float height = 1.72f;
    float jump = 0;       // altura dos pés acima do chão
    float crouch = 0;     // 0..1
    float lean = 0;       // rad, + = para a direita do jogador
    bool handsAuto = true;        // braços caídos ao lado do corpo
    V2 leftHand, rightHand;       // posições absolutas no plano de jogo (se !handsAuto)
};

struct SimScene {
    float cameraHeight = 1.0f;
    float pitchDeg = -4.f;   // negativo = olhando para baixo
    float wallZ = 4.6f;
    bool noise = true;
    bool fast = false;       // 1/4 da resolução (suficiente para o rastreador; mais leve)
    Intrinsics intr;         // 640x480, f = 580
};

class SimDepth {
public:
    // Gera 640x480 em mm. 'poses' pode ter 0..2 entradas.
    void render(const SimScene &scene, const SimPose *poses, int count, std::vector<uint16_t> &outMm);

    // Pontos de referência do boneco (para testes): mãos e topo da cabeça no plano de jogo.
    struct Skeleton { V2 head, leftHand, rightHand, leftShoulder, rightShoulder; float headTop; };
    static Skeleton skeleton(const SimPose &p);

private:
    uint32_t rng_ = 12345;
    float noise();
};

} // namespace k3d
