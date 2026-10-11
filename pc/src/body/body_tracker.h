#pragma once
// Rastreador corporal a partir da profundidade do Kinect v1.
//
// O libfreenect não fornece esqueleto (isso existia só no SDK da Microsoft
// para Windows). Este rastreador trabalha com a geometria real medida:
//   1. estima o plano do chão (RANSAC) e remove o chão;
//   2. separa pessoas por componentes conexos com continuidade de profundidade;
//   3. acompanha até 2 jogadores e mede altura, pulo, agachamento,
//      inclinação, mãos, batida de braços e a silhueta no plano de jogo.
// As mãos são estimadas pelos extremos da silhueta, não são articulações medidas.
#include <cstdint>
#include <array>
#include <deque>
#include <vector>

#include "body_types.h"

namespace k3d {

// Fornece a cor RGB correspondente a um pixel de profundidade (opcional).
class ColorLookup {
public:
    virtual ~ColorLookup() = default;
    virtual bool color(int u, int v, uint16_t mm, uint8_t out[3]) const = 0;
};

// Rastreamento externo (NiTE2): rótulo de usuário por pixel + esqueletos, no mesmo quadro da profundidade.
// Coordenadas da câmera em metros, com Y PARA BAIXO (mesma convenção dos pontos de profundidade).
struct ExternalUser {
    int id = 0;
    bool skeleton = false;
    std::array<V3, PlayerBody::kJoints> joints{};
    std::array<float, PlayerBody::kJoints> conf{};
};
struct ExternalBodies {
    const uint16_t *userMap = nullptr;  // intr.width x intr.height, 0 = ninguém
    std::vector<ExternalUser> users;
    bool floorValid = false;
    V3 floorPoint, floorNormal;         // normal apontando para cima (y < 0)
};

struct TrackerParams {
    int step = 4;                 // subamostragem da imagem 640x480 -> 160x120
    float minZ = 0.5f, maxZ = 4.5f;
    float floorInlier = 0.03f;    // m
    float floorCut = 0.05f;       // pixels a menos de 5 cm do chão são chão
    int floorEveryFrames = 10;
    int confirmFrames = 4;        // quadros até um candidato virar jogador
    int keepMissingFrames = 8;    // quadros sem ver antes de esquecer
};

class BodyTracker {
public:
    explicit BodyTracker(TrackerParams p = {}) : p_(p) {}

    // depthMm: imagem intr.width x intr.height em milímetros (0 = sem medida).
    // ext: segmentação e esqueletos do NiTE2 (opcional). Sem ele, tudo vem da geometria.
    const BodyFrame &update(const uint16_t *depthMm, const Intrinsics &intr, double timeSeconds,
                            const ColorLookup *color = nullptr, const ExternalBodies *ext = nullptr);
    const BodyFrame &frame() const { return frame_; }
    void reset();

    // Diagnóstico / visualização na tela de sensores (160x120): 0 nada, 1 chão, 2 outro, 10+i jogador i
    const std::vector<uint8_t> &labelImage() const { return labelsOut_; }
    int labelWidth() const { return gw_; }
    int labelHeight() const { return gh_; }

private:
    struct Pix { float x, y, z; int u, v; };   // x,y,z plano de jogo; u,v na grade reduzida
    struct Comp {
        std::vector<int> idx;
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, sumZ = 0;
        bool touchTop = false, touchBottom = false, touchLeft = false, touchRight = false;
        float meanX() const { return 0.5f * (minX + maxX); }
        int extId = 0;
    };
    struct History { double t; float headY, bottomY, armsY; };
    struct Track {
        int id = -1;
        int slot = -1;           // índice em frame_.players, -1 enquanto pendente
        int extId = 0;           // id do usuário no NiTE2
        int confirm = 0;
        int missing = 0;
        float x = 0, z = 0;
        double firstSeen = 0;
        std::deque<History> hist;
        double lastJump = -10, lastFlap = -10;
        bool airborne = false;
        PlayerBody body;
        bool hasBody = false;
    };

    void estimateFloor(const std::vector<V3> &pts, const std::vector<uint8_t> &lowerHalf);
    void measure(Track &t, const Comp &c, double now, float dt, const uint16_t *depthMm,
                 const Intrinsics &intr, const ColorLookup *color, const ExternalUser *skel);
    V3 toPlay(const V3 &cam) const;
    V3 basisN_, basisRight_, basisFwd_;
    float basisD_ = 0;

    TrackerParams p_;
    BodyFrame frame_;
    std::vector<Track> tracks_;
    int nextId_ = 1;
    uint64_t frames_ = 0;
    double lastTime_ = -1;
    uint32_t rng_ = 0x9E3779B9u;

    bool floorValid_ = false;
    V3 n_{0, -1, 0};
    float d_ = 0.8f;

    int gw_ = 0, gh_ = 0;
    std::vector<V3> cam_;        // pontos na câmera (m), z=0 inválido
    std::vector<Pix> pix_;
    std::vector<int> label_;
    std::vector<uint8_t> labelsOut_;
};

} // namespace k3d
