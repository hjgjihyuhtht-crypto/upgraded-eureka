#pragma once
// Conversões para exibição. Nunca alteram os dados originais: escrevem em um
// buffer RGBA separado. Código puro, testado no host.
#include <cstdint>
#include <vector>
#include "calibration.h"

namespace k3d {

enum class Colormap : int { Gray = 0, Turbo = 1 };
enum class DepthFilter : int { None = 0, Median3 = 1, RemoveEdges = 2, MedianAndEdges = 3 };

struct DepthViewParams {
    int minMm = 500;
    int maxMm = 4500;
    Colormap colormap = Colormap::Turbo;
    DepthFilter filter = DepthFilter::None;
};

struct VideoViewParams {
    float brightness = 0.f;  // -1..1 (somente visualização)
    float contrast = 1.f;    // 0.2..4 (somente visualização)
};

// Profundidade bruta -> mm usando a tabela de calibração. Sem medida -> 0.
void rawToMm(const uint16_t *raw, size_t n, const Calibration &cal, uint16_t *outMm);

// Filtros de ruído em mm. Não preenchem buracos: pixels sem medida continuam 0.
// Retorna quantos pixels válidos foram removidos (bordas/pixels "voadores").
size_t filterDepthMm(const uint16_t *inMm, int w, int h, DepthFilter f, uint16_t *outMm);

void depthMmToRgba(const uint16_t *mm, int w, int h, const DepthViewParams &p, uint8_t *rgba, int strideBytes);
void rgbToRgba(const uint8_t *rgb, int w, int h, const VideoViewParams &p, uint8_t *rgba, int strideBytes);
void grayToRgba(const uint8_t *gray, int w, int h, const VideoViewParams &p, uint8_t *rgba, int strideBytes);

// RGB reamostrado na grade da profundidade (registro pela calibração de fábrica).
// Pixels sem profundidade ou fora da imagem RGB ficam pretos. Retorna pixels coloridos.
size_t alignRgbToDepth(const uint16_t *mm, const uint8_t *rgb, const Calibration &cal, uint8_t *outRgb);

// Turbo (Google, aproximação polinomial), t em [0,1].
void turbo(float t, uint8_t &r, uint8_t &g, uint8_t &b);

} // namespace k3d
