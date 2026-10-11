#pragma once
// Tipos do rastreamento corporal. Código puro (sem raylib, sem libfreenect).
//
// Convenções do "plano de jogo":
//   x  metros, lateral, POSITIVO = direita do jogador (como num espelho visto por trás)
//   y  metros acima do chão
//   z  metros, distância do jogador até o sensor
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace k3d {

struct V2 {
    float x = 0, y = 0;
    V2() = default;
    V2(float x_, float y_) : x(x_), y(y_) {}
    V2 operator+(V2 o) const { return {x + o.x, y + o.y}; }
    V2 operator-(V2 o) const { return {x - o.x, y - o.y}; }
    V2 operator*(float s) const { return {x * s, y * s}; }
    float len() const { return std::sqrt(x * x + y * y); }
};

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() = default;
    V3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    V3 operator+(V3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(V3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(V3 o) const { return x * o.x + y * o.y + z * o.z; }
    V3 cross(V3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float len() const { return std::sqrt(dot(*this)); }
    V3 norm() const { float l = len(); return l > 1e-9f ? *this * (1.f / l) : V3{0, 0, 0}; }
};

struct Intrinsics {
    float fx = 580.f, fy = 580.f, cx = 320.f, cy = 240.f;
    int width = 640, height = 480;
};

// Grade de ocupação do corpo no plano de jogo (resolução 4 cm).
struct OccupancyGrid {
    static constexpr float kCell = 0.04f;
    static constexpr float kMinX = -2.0f, kMaxX = 2.0f;
    static constexpr float kMaxY = 2.6f;
    static constexpr int kW = 100;  // (kMaxX-kMinX)/kCell
    static constexpr int kH = 65;   // kMaxY/kCell
    std::array<uint8_t, kW * kH> cells{};

    void clear() { cells.fill(0); }
    static int cx(float x) { return static_cast<int>(std::floor((x - kMinX) / kCell)); }
    static int cy(float y) { return static_cast<int>(std::floor(y / kCell)); }
    bool at(int ix, int iy) const { return ix >= 0 && iy >= 0 && ix < kW && iy < kH && cells[iy * kW + ix]; }
    void set(int ix, int iy) { if (ix >= 0 && iy >= 0 && ix < kW && iy < kH) cells[iy * kW + ix] = 1; }
    static V2 center(int ix, int iy) { return {kMinX + (ix + 0.5f) * kCell, (iy + 0.5f) * kCell}; }

    struct Hit { int cells = 0; V2 centroid; V2 normal; };
    // Células ocupadas dentro do círculo (centro em metros, deslocado por 'offset').
    Hit overlapCircle(V2 c, float r, V2 offset = {}) const;
    // Células ocupadas dentro do retângulo [x0,x1]x[y0,y1].
    int overlapRect(float x0, float y0, float x1, float y1, V2 offset = {}) const;
    int count() const;
};

enum GuidanceFlags : uint32_t {
    G_NONE = 0,
    G_TOO_CLOSE = 1 << 0,
    G_TOO_FAR = 1 << 1,
    G_LEFT_EDGE = 1 << 2,
    G_RIGHT_EDGE = 1 << 3,
    G_HEAD_CUT = 1 << 4,
    G_FEET_CUT = 1 << 5,
};

struct PlayerBody {
    bool present = false;
    int trackId = -1;
    float seenSeconds = 0;  // há quanto tempo é rastreado

    // Posição
    float x = 0;      // lateral do tronco (m)
    float z = 0;      // distância ao sensor (m)
    float headY = 0;  // topo da cabeça (m)
    float standY = 0; // altura em pé (referência aprendida)
    float bottomY = 0;
    float lean = 0;   // rad, positivo = inclinado para a direita do jogador

    // Ações
    bool airborne = false;
    bool jumpEvent = false;  // borda de subida do pulo neste quadro
    float jumpHeight = 0;    // altura dos pés no pulo atual (m)
    float crouch = 0;        // 0 em pé .. 1 bem agachado
    bool ducking = false;

    V2 leftHand, rightHand;           // no plano de jogo
    V2 leftHandVel, rightHandVel;     // m/s
    bool handsUp = false;             // as duas mãos acima da cabeça
    bool flapEvent = false;           // batida de braços para baixo
    float flapStrength = 0;           // 0..1
    float armsSpread = 0;             // distância entre as mãos (m)

    uint32_t guidance = G_NONE;
    float visibleTopY = 2.0f;   // altura do topo da imagem na distância do jogador
    float visibleMinX = -1.4f, visibleMaxX = 1.4f;  // faixa lateral visível nessa distância
    // Altura máxima que o sensor ainda enxerga ao esticar o braço (para posicionar alvos).
    float reachTop() const { return std::fmin(standY + 0.32f, visibleTopY - 0.10f); }

    // Esqueleto medido pelo NiTE2 (quando disponível). Pontos no plano de jogo (x, y, z).
    // Ordem: cabeça, pescoço, ombro E/D, cotovelo E/D, mão E/D, tronco, quadril E/D, joelho E/D, pé E/D
    // (E = esquerda do jogador, x negativo).
    static constexpr int kJoints = 15;
    bool skeleton = false;
    std::array<V3, kJoints> joints{};
    std::array<float, kJoints> jointConf{};

    OccupancyGrid grid;               // corpo inteiro no plano de jogo
    // Silhueta fina (2 cm) para desenho: 200 x 130, valor 0 = vazio, 1..255 = presença.
    static constexpr int kSilW = 200, kSilH = 130;
    static constexpr float kSilCell = 0.02f;
    std::vector<uint8_t> silhouette;  // kSilW*kSilH
    std::vector<uint8_t> silhouetteRgb; // kSilW*kSilH*3 (vazio se não houver cor)
};

struct BodyFrame {
    static constexpr int kMaxPlayers = 2;
    std::array<PlayerBody, kMaxPlayers> players;
    bool floorFound = false;
    V3 floorNormal;   // no sistema da câmera
    float floorD = 0; // n·p + d = altura
    float cameraHeight = 0;
    float cameraPitchDeg = 0;
    double time = 0;
    uint64_t frameSeq = 0;
    bool simulated = false;
    bool skeletonSource = false;  // corpos vêm do NiTE2 (segmentação + esqueleto)

    int count() const { int n = 0; for (auto &p : players) n += p.present; return n; }
    const PlayerBody *first() const { for (auto &p : players) if (p.present) return &p; return nullptr; }
};

} // namespace k3d
