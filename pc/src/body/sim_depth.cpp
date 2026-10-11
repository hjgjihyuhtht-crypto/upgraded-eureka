#include "sim_depth.h"

#include <algorithm>
#include <cmath>

namespace k3d {

namespace {
struct Capsule { V3 a, b; float r; };

// Interseção raio (origem o, direção d não normalizada) x cápsula. Retorna t>0 ou -1.
float rayCapsule(V3 o, V3 d, const Capsule &c) {
    const V3 ba = c.b - c.a, oa = o - c.a;
    const float baba = ba.dot(ba), bard = ba.dot(d), baoa = ba.dot(oa);
    const float rdoa = d.dot(oa), oaoa = oa.dot(oa), dd = d.dot(d);
    const float A = baba * dd - bard * bard;
    const float B = baba * rdoa - baoa * bard;
    const float C = baba * oaoa - baoa * baoa - c.r * c.r * baba;
    float h = B * B - A * C;
    if (A > 1e-9f && h >= 0) {
        const float t = (-B - std::sqrt(h)) / A;
        const float y = baoa + t * bard;
        if (y > 0 && y < baba && t > 0) return t;
        // tampas esféricas
        const V3 oc = y <= 0 ? oa : o - c.b;
        const float b2 = d.dot(oc), c2 = oc.dot(oc) - c.r * c.r;
        h = b2 * b2 - dd * c2;
        if (h > 0) {
            const float t2 = (-b2 - std::sqrt(h)) / dd;
            if (t2 > 0) return t2;
        }
    } else {
        // cápsula degenerada (esfera)
        const float b2 = d.dot(oa), c2 = oaoa - c.r * c.r;
        h = b2 * b2 - dd * c2;
        if (h > 0) {
            const float t2 = (-b2 - std::sqrt(h)) / dd;
            if (t2 > 0) return t2;
        }
    }
    return -1;
}

struct Body {
    std::vector<Capsule> caps;
    V3 bbMin, bbMax;
};

V3 W(V2 g, float z) { return V3(-g.x, g.y, z); }  // plano de jogo -> mundo (x do mundo = direita da câmera)

V2 clampReach(V2 shoulder, V2 hand, float reach) {
    V2 d = hand - shoulder;
    const float l = d.len();
    if (l > reach) d = d * (reach / l);
    return shoulder + d;
}

V2 rot(V2 p, V2 pivot, float a) {
    // gira no plano (x, y) em torno do pivô; a > 0 leva o topo para +x
    const V2 d = p - pivot;
    const float c = std::cos(a), s = std::sin(a);
    return pivot + V2(d.x * c + d.y * s, -d.x * s + d.y * c);
}
} // namespace

SimDepth::Skeleton SimDepth::skeleton(const SimPose &p) {
    const float s = p.height / 1.72f;
    const float hipY = p.jump + 0.90f * s * (1.f - 0.40f * p.crouch);
    const V2 hip(p.x, hipY);
    auto up = [&](V2 q) { return rot(q, hip, p.lean); };
    const V2 lS = up({p.x - 0.19f * s, hipY + 0.50f * s});
    const V2 rS = up({p.x + 0.19f * s, hipY + 0.50f * s});
    const V2 head = up({p.x, hipY + 0.69f * s});
    V2 lH, rH;
    if (p.handsAuto) {
        lH = up({p.x - 0.26f * s, hipY - 0.04f * s});
        rH = up({p.x + 0.26f * s, hipY - 0.04f * s});
    } else {
        lH = clampReach(lS, p.leftHand, 0.66f * s);
        rH = clampReach(rS, p.rightHand, 0.66f * s);
    }
    return {head, lH, rH, lS, rS, head.y + 0.115f * s};
}

float SimDepth::noise() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFF) / 65535.f - 0.5f;
}

void SimDepth::render(const SimScene &sc, const SimPose *poses, int count, std::vector<uint16_t> &out) {
    const Intrinsics &in = sc.intr;
    out.assign(static_cast<size_t>(in.width) * in.height, 0);

    std::vector<Body> bodies;
    for (int i = 0; i < count; ++i) {
        const SimPose &p = poses[i];
        if (!p.present) continue;
        const float s = p.height / 1.72f;
        const Skeleton k = skeleton(p);
        const float z = p.z;
        const float hipY = p.jump + 0.90f * s * (1.f - 0.40f * p.crouch);
        const V2 hip(p.x, hipY);
        const V2 chest = rot({p.x, hipY + 0.42f * s}, hip, p.lean);
        const V2 neck = rot({p.x, hipY + 0.56f * s}, hip, p.lean);
        Body b;
        auto add = [&](V2 a, float za, V2 c, float zc, float r) { b.caps.push_back({W(a, za), W(c, zc), r}); };
        // tronco (duas cápsulas lado a lado para ficar mais largo que fundo)
        add(hip + V2(-0.07f * s, 0.05f * s), z, chest + V2(-0.07f * s, 0), z, 0.12f * s);
        add(hip + V2(0.07f * s, 0.05f * s), z, chest + V2(0.07f * s, 0), z, 0.12f * s);
        add(neck, z, k.head, z, 0.06f * s);
        add(k.head, z, k.head, z, 0.11f * s);
        // braços
        auto arm = [&](V2 sh, V2 hand) {
            V2 mid = (sh + hand) * 0.5f;
            const V2 out = V2(mid.x - p.x, 0);
            mid = mid + out * 0.15f + V2(0, -0.04f * s);
            add(sh, z, mid, z - 0.03f, 0.05f * s);
            add(mid, z - 0.03f, hand, z - 0.06f, 0.045f * s);
            add(hand, z - 0.06f, hand, z - 0.06f, 0.055f * s);
        };
        arm(k.leftShoulder, k.leftHand);
        arm(k.rightShoulder, k.rightHand);
        // pernas: joelho vai para frente ao agachar
        for (int side = -1; side <= 1; side += 2) {
            const V2 h = hip + V2(side * 0.10f * s, 0);
            const V2 foot(p.x + side * 0.13f * s, p.jump + 0.05f);
            const V2 knee((h.x + foot.x) * 0.5f, (h.y + foot.y) * 0.5f);
            const float kz = z - 0.35f * p.crouch * s;
            add(h, z, knee, kz, 0.08f * s);
            add(knee, kz, foot, z, 0.06f * s);
            add(foot, z, foot + V2(0, 0), z - 0.12f * s, 0.05f * s);
        }
        b.bbMin = V3(1e9f, 1e9f, 1e9f);
        b.bbMax = V3(-1e9f, -1e9f, -1e9f);
        for (auto &c : b.caps)
            for (V3 q : {c.a, c.b}) {
                b.bbMin = V3(std::min(b.bbMin.x, q.x - c.r), std::min(b.bbMin.y, q.y - c.r), std::min(b.bbMin.z, q.z - c.r));
                b.bbMax = V3(std::max(b.bbMax.x, q.x + c.r), std::max(b.bbMax.y, q.y + c.r), std::max(b.bbMax.z, q.z + c.r));
            }
        bodies.push_back(std::move(b));
    }

    const float th = sc.pitchDeg * 3.14159265f / 180.f;
    const float ct = std::cos(th), stt = std::sin(th);
    const V3 camPos(0, sc.cameraHeight, 0);
    // Renderiza em resolução reduzida e replica em blocos.
    const int bs = sc.fast ? 4 : 2;
    const int hw = in.width / bs, hh = in.height / bs;
    for (int v = 0; v < hh; ++v) {
        for (int u = 0; u < hw; ++u) {
            const float uc = u * bs + bs / 2, vc = v * bs + bs / 2;
            // direção na câmera (z = 1) -> mundo (y para cima, inclinação)
            const float dx = (uc - in.cx) / in.fx, dy = (vc - in.cy) / in.fy;
            const float wy0 = -dy, wz0 = 1.f;
            const V3 d(dx, wy0 * ct + wz0 * stt, -wy0 * stt + wz0 * ct);
            float best = 1e9f;
            if (d.y < -1e-5f) best = std::min(best, -camPos.y / d.y);   // chão
            if (d.z > 1e-5f) best = std::min(best, sc.wallZ / d.z);     // parede
            for (auto &b : bodies) {
                // teste rápido com a caixa envolvente
                float t0 = 0, t1 = best;
                bool hitBox = true;
                for (int ax = 0; ax < 3 && hitBox; ++ax) {
                    const float o = ax == 0 ? camPos.x : ax == 1 ? camPos.y : camPos.z;
                    const float dd = ax == 0 ? d.x : ax == 1 ? d.y : d.z;
                    const float mn = ax == 0 ? b.bbMin.x : ax == 1 ? b.bbMin.y : b.bbMin.z;
                    const float mx = ax == 0 ? b.bbMax.x : ax == 1 ? b.bbMax.y : b.bbMax.z;
                    if (std::fabs(dd) < 1e-7f) { if (o < mn || o > mx) hitBox = false; continue; }
                    float ta = (mn - o) / dd, tb = (mx - o) / dd;
                    if (ta > tb) std::swap(ta, tb);
                    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
                    if (t0 > t1) hitBox = false;
                }
                if (!hitBox) continue;
                for (auto &c : b.caps) {
                    const float t = rayCapsule(camPos, d, c);
                    if (t > 0 && t < best) best = t;
                }
            }
            if (best >= 1e8f) continue;
            // profundidade = componente ao longo do eixo óptico (d foi construído com z_câmera = 1)
            float zMeters = best;
            if (zMeters > 8.f) continue;
            if (sc.noise) {
                zMeters += noise() * 0.0035f * zMeters * zMeters;
                if ((rng_ & 0x3FF) < 4) continue;  // ~0,4% sem medida
            }
            const uint16_t mm = static_cast<uint16_t>(std::clamp(zMeters * 1000.f, 1.f, 9999.f));
            for (int yy = 0; yy < bs; ++yy)
                for (int xx = 0; xx < bs; ++xx) out[(v * bs + yy) * in.width + u * bs + xx] = mm;
        }
    }
}

} // namespace k3d
