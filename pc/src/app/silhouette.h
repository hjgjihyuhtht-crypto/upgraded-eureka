#pragma once
// Transforma a silhueta medida pelo Kinect em textura (com brilho na borda)
// e desenha como um "cartão" no plano do jogador dentro da cena 3D.
#include "raylib.h"

#include "body/body_types.h"

namespace k3d {

class SilhouetteRenderer {
public:
    void init();
    void shutdown();
    // Atualiza as texturas a partir do quadro do rastreador (chamar quando muda).
    void update(const BodyFrame &f, bool useColor);
    // Desenha o jogador i no mundo 3D: o canto (x=-2, y=0) do plano vai para 'origin'.
    // 'scale' encolhe/aumenta; 'alpha' 0..1.
    void draw3D(int i, Vector3 origin, float scale = 1.f, float alpha = 1.f, float extraX = 0.f) const;
    // Desenha em 2D (ex.: tela de sensores e cartões de resultado).
    void draw2D(int i, Rectangle dst, float alpha = 1.f) const;
    bool has(int i) const { return present_[i]; }
    Color color(int i) const;

private:
    Texture2D tex_[2]{};
    Image img_[2]{};
    bool present_[2] = {false, false};
    bool ok_ = false;
};

} // namespace k3d
