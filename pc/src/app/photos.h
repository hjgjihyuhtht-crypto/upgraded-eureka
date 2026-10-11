#pragma once
// Fotos tiradas durante as partidas: cena do jogo + foto da câmera RGB (quando houver).
#include <string>
#include <vector>

#include "raylib.h"

namespace k3d {

class App;

struct PhotoResult {
    std::string path;
    Texture2D thumb{};
    bool ok = false;
};

// Salva a foto composta. 'scene' é a imagem do jogo (já na orientação certa).
PhotoResult savePhoto(App &app, const Image &scene, const std::string &gameName, const std::string &caption);

// Carrega PNG/JPG de um caminho UTF-8 como textura (redimensiona para caber em maxW x maxH).
Texture2D loadTextureUtf8(const std::string &path, int maxW = 0, int maxH = 0);

} // namespace k3d
