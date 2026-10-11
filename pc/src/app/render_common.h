#pragma once
// Recursos 3D compartilhados pelos jogos: shader com luz e neblina, malhas e utilidades.
#include "raylib.h"

namespace k3d {

class App;

struct RenderInfo {
    float time = 0;
    float shake = 0;
    int width = 1280, height = 720;
};

struct World3D {
    Shader lit{};
    int locLightDir = -1, locViewPos = -1, locFogColor = -1, locFogNear = -1, locFogFar = -1, locAmbient = -1;
    Model sphere{}, cube{}, cylinder{}, cone{}, torus{}, diamond{}, plane{};
    bool ok = false;

    void init();
    void shutdown();
    void begin(const Camera3D &cam, Color fog, float fogNear, float fogFar, Vector3 lightDir, float ambient = 0.45f);
    void end();
};

World3D &world3d();

// Formas com iluminação (usam os modelos acima; cor por instância)
void drawSphere(Vector3 p, float r, Color c);
void drawEllipsoid(Vector3 p, Vector3 radii, Color c);
void drawBox(Vector3 p, Vector3 size, Color c);
void drawCylinderY(Vector3 base, float r, float h, Color c);
void drawCone(Vector3 base, float r, float h, Color c);
void drawGem(Vector3 p, float size, float spin, Color c);
void drawTorus(Vector3 p, float radius, Vector3 axis, float angleDeg, Color c);
void drawSky(int w, int h, Color top, Color bottom);
Camera3D shakeCamera(Camera3D c, float shake, float t);

} // namespace k3d
