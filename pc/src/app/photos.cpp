#include "app/photos.h"

#include <ctime>

#include "app/app.h"

namespace k3d {

Texture2D loadTextureUtf8(const std::string &path, int maxW, int maxH) {
    std::vector<unsigned char> data;
    if (!readFileUtf8(path, data) || data.empty()) return Texture2D{};
    const std::string ext = path.size() > 4 && (path.substr(path.size() - 4) == ".jpg" || path.substr(path.size() - 4) == ".JPG") ? ".jpg" : ".png";
    Image img = LoadImageFromMemory(ext.c_str(), data.data(), static_cast<int>(data.size()));
    if (!img.data) return Texture2D{};
    if (maxW > 0 && maxH > 0 && (img.width > maxW || img.height > maxH)) {
        const float k = std::min(static_cast<float>(maxW) / img.width, static_cast<float>(maxH) / img.height);
        ImageResize(&img, static_cast<int>(img.width * k), static_cast<int>(img.height * k));
    }
    Texture2D t = LoadTextureFromImage(img);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);  // fotos não têm tamanho potência de 2 (OpenGL ES 2)
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    return t;
}

PhotoResult savePhoto(App &app, const Image &scene, const std::string &gameName, const std::string &caption) {
    PhotoResult r;
    const int W = 1280, H = 720;
    Image out = GenImageColor(W, H, Color{12, 24, 40, 255});
    // câmera RGB real, quando houver Kinect (ou gravação com RGB)
    Image cam{};
    bool haveCam = false;
    {
        std::vector<uint8_t> rgb;
        int cw = 0, chh = 0;
        if (app.copyCameraRgb(rgb, cw, chh)) {
            cam = GenImageColor(cw, chh, BLACK);
            auto *px = static_cast<Color *>(cam.data);
            for (int i = 0; i < cw * chh; ++i) px[i] = Color{rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2], 255};
            haveCam = true;
        }
    }
    const int capH = 90;
    if (haveCam) {
        // jogo à esquerda (maior), foto da câmera à direita
        ImageDraw(&out, scene, {0, 0, static_cast<float>(scene.width), static_cast<float>(scene.height)},
                  {20, 20, 820, static_cast<float>(H - capH - 40)}, WHITE);
        ImageDraw(&out, cam, {0, 0, static_cast<float>(cam.width), static_cast<float>(cam.height)},
                  {860, 20 + 60, 400, 300}, WHITE);
        ImageDrawRectangleLines(&out, {858, 78, 404, 304}, 4, Color{255, 209, 102, 255});
        ImageDrawTextEx(&out, app.ui.fontBold(), "Você!", {860, 30}, 40, 1, Color{255, 209, 102, 255});
        UnloadImage(cam);
    } else {
        ImageDraw(&out, scene, {0, 0, static_cast<float>(scene.width), static_cast<float>(scene.height)},
                  {20, 20, static_cast<float>(W - 40), static_cast<float>(H - capH - 40)}, WHITE);
    }
    // legenda
    ImageDrawRectangle(&out, 0, H - capH, W, capH, Color{46, 196, 182, 255});
    char date[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(date, sizeof(date), "%d/%m/%Y %H:%M", std::localtime(&t));
    ImageDrawTextEx(&out, app.ui.fontBold(), caption.c_str(), {28, static_cast<float>(H - capH + 12)}, 38, 1, Color{8, 30, 40, 255});
    const std::string sub = std::string("Expedição Ativa · ") + gameName + " · " + date + (app.simulated() ? " · SIMULAÇÃO" : "");
    ImageDrawTextEx(&out, app.ui.fontRegular(), sub.c_str(), {30, static_cast<float>(H - capH + 54)}, 24, 1, Color{10, 50, 60, 255});

    int size = 0;
    unsigned char *png = ExportImageToMemory(out, ".png", &size);
    static int counter = 0;
    char name[96];
    std::snprintf(name, sizeof(name), "/foto_%s_%02d.png", nowStamp().c_str(), counter++ % 100);
    r.path = app.paths.photos + name;
    r.ok = png && writeFileUtf8(r.path, png, static_cast<size_t>(size));
    if (png) MemFree(png);
    if (r.ok) {
        Image th = ImageCopy(out);
        ImageResize(&th, 320, 180);
        r.thumb = LoadTextureFromImage(th);
        SetTextureFilter(r.thumb, TEXTURE_FILTER_BILINEAR);
        UnloadImage(th);
    }
    UnloadImage(out);
    return r;
}

} // namespace k3d
