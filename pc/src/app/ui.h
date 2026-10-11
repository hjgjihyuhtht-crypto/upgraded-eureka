#pragma once
// Interface: fontes, cores, botões e o cursor de mão (gesto de "segurar" sobre o botão).
#include <map>
#include <string>
#include <vector>

#include "raylib.h"

namespace k3d {

struct BodyFrame;

namespace theme {
inline const Color bg{12, 24, 40, 255};
inline const Color bg2{20, 40, 64, 255};
inline const Color panel{24, 46, 74, 235};
inline const Color teal{46, 196, 182, 255};
inline const Color orange{255, 159, 28, 255};
inline const Color yellow{255, 209, 102, 255};
inline const Color pink{239, 71, 111, 255};
inline const Color violet{131, 106, 255, 255};
inline const Color text{240, 246, 252, 255};
inline const Color muted{158, 178, 200, 255};
inline const Color bronze{205, 127, 50, 255};
inline const Color silver{200, 208, 218, 255};
inline const Color gold{255, 200, 40, 255};
inline const Color platinum{170, 235, 255, 255};
inline const Color player1{56, 220, 255, 255};
inline const Color player2{255, 140, 60, 255};
} // namespace theme

enum class Align { Left, Center, Right };
enum class BtnStyle { Primary, Secondary, Danger, Card };

class Ui {
public:
    void load();
    void unload();

    // Chamar uma vez por quadro, antes de desenhar a cena.
    void beginFrame(const BodyFrame &body, bool handCursor, float dt);
    // Desenha o cursor da mão por cima de tudo.
    void endFrame();

    float s() const { return scale_; }          // escala da tela (1 = 720 px de altura)
    float px(float v) const { return v * scale_; }
    int w() const { return GetScreenWidth(); }
    int h() const { return GetScreenHeight(); }

    bool button(const std::string &id, Rectangle r, const std::string &label, BtnStyle st = BtnStyle::Primary,
                bool enabled = true, const std::string &sub = "");
    bool backPressed() const { return back_; }

    void text(const std::string &t, float x, float y, float size, Color c, Align a = Align::Left, bool bold = false);
    void textShadow(const std::string &t, float x, float y, float size, Color c, Align a = Align::Left, bool bold = true);
    float textWidth(const std::string &t, float size, bool bold = false);
    // Quebra de linha dentro da largura; devolve a altura usada.
    float textBox(const std::string &t, Rectangle r, float size, Color c, Align a = Align::Left, bool bold = false);
    void panel(Rectangle r, Color c = theme::panel, float round = 0.12f);
    void background(Color top = theme::bg2, Color bottom = theme::bg);
    void medalIcon(int medal, Vector2 center, float radius);
    void badge(const std::string &t, float x, float y, Color bg, Color fg);

    Font &fontBold() { return bold_; }
    Font &fontRegular() { return regular_; }
    // Posição do cursor de mão na tela (se ativo)
    bool handActive() const { return handActive_; }
    Vector2 hand() const { return hand_; }

private:
    Font bold_{}, regular_{};
    float scale_ = 1;
    bool back_ = false;
    // navegação por teclado
    std::vector<std::string> order_, prevOrder_;
    std::string focus_;
    bool activate_ = false;
    int navDelta_ = 0;
    bool mouseMoved_ = false;
    Vector2 lastMouse_{};
    // mão
    bool handEnabled_ = false, handActive_ = false;
    Vector2 hand_{}, handSmooth_{};
    std::map<std::string, float> hover_;
    std::string hovered_;
    float hoverProgress_ = 0;
    float dt_ = 0;
};

} // namespace k3d
