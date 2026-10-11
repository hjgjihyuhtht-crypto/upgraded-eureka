#pragma once
// Calibração do Kinect v1 independente do dispositivo: permite converter
// profundidade bruta em mm, alinhar RGB à profundidade e projetar em 3D
// tanto ao vivo quanto ao reproduzir uma gravação.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct _freenect_device;

namespace k3d {

constexpr int kDepthW = 640;
constexpr int kDepthH = 480;
constexpr uint16_t kRawNoValue = 2047;   // FREENECT_DEPTH_RAW_NO_VALUE
constexpr int kRawLutSize = 2048;        // FREENECT_DEPTH_RAW_MAX_VALUE
constexpr int kMaxMetricMm = 10000;      // FREENECT_DEPTH_MM_MAX_VALUE
constexpr int kRegXScale = 256;          // REG_X_VAL_SCALE do libfreenect

struct Point3 { double x, y, z; };

struct Calibration {
    bool valid = false;
    // Parâmetros de fábrica lidos do sensor
    float dcmosEmitterDistCm = 0;
    float dcmosRcmosDistCm = 0;
    float referenceDistanceMm = 0;   // distância focal do IR em mm
    float referencePixelSizeMm = 0;  // tamanho do pixel no plano zero (imagem 1280x1024)
    uint32_t targetOffset = 0;

    std::vector<uint16_t> rawToMm;          // kRawLutSize
    std::vector<int32_t> depthToRgbShift;   // kMaxMetricMm
    std::vector<int32_t> regTable;          // kDepthW*kDepthH*2 (x escalado, y)

    // Intrínsecos equivalentes (modelo pinhole) para a imagem 640x480
    double fx() const { return referencePixelSizeMm > 0 ? referenceDistanceMm / (2.0 * referencePixelSizeMm) : 0; }
    double fy() const { return fx(); }
    double cx() const { return kDepthW / 2.0; }
    double cy() const { return kDepthH / 2.0; }

    uint16_t mm(uint16_t raw) const { return raw < kRawLutSize && !rawToMm.empty() ? rawToMm[raw] : 0; }

    // Retroprojeção: (u, v, Z) -> (X, Y, Z) em mm, eixo Y para baixo (convenção da câmera).
    Point3 toWorld(double u, double v, double zMm) const {
        const double f = fx();
        if (f <= 0) return {0, 0, 0};
        return {(u - cx()) * zMm / f, (v - cy()) * zMm / f, zMm};
    }

    // Pixel RGB correspondente a um pixel de profundidade; false se fora da imagem.
    bool depthToRgbPixel(int x, int y, uint16_t mm, int &rx, int &ry) const;

    static Calibration fromDevice(_freenect_device *dev);
    std::vector<uint8_t> serialize() const;
    static Calibration deserialize(const uint8_t *data, size_t len);
    std::string intrinsicsJson() const;
};

} // namespace k3d
