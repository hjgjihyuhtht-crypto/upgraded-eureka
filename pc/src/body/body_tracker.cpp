#include "body_tracker.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace k3d {

// ------------------------------------------------------------ OccupancyGrid

OccupancyGrid::Hit OccupancyGrid::overlapCircle(V2 c, float r, V2 off) const {
    Hit h;
    const V2 lc = c - off;
    const int x0 = cx(lc.x - r), x1 = cx(lc.x + r), y0 = cy(lc.y - r), y1 = cy(lc.y + r);
    float sx = 0, sy = 0;
    for (int iy = std::max(0, y0); iy <= std::min(kH - 1, y1); ++iy)
        for (int ix = std::max(0, x0); ix <= std::min(kW - 1, x1); ++ix) {
            if (!cells[iy * kW + ix]) continue;
            const V2 p = center(ix, iy);
            if ((p - lc).len() > r) continue;
            ++h.cells;
            sx += p.x; sy += p.y;
        }
    if (h.cells) {
        h.centroid = V2{sx / h.cells, sy / h.cells} + off;
        V2 n = c - h.centroid;
        const float l = n.len();
        h.normal = l > 1e-4f ? n * (1.f / l) : V2{0, 1};
    }
    return h;
}

int OccupancyGrid::overlapRect(float x0, float y0, float x1, float y1, V2 off) const {
    int n = 0;
    const int ix0 = std::max(0, cx(x0 - off.x)), ix1 = std::min(kW - 1, cx(x1 - off.x));
    const int iy0 = std::max(0, cy(y0 - off.y)), iy1 = std::min(kH - 1, cy(y1 - off.y));
    for (int iy = iy0; iy <= iy1; ++iy)
        for (int ix = ix0; ix <= ix1; ++ix) n += cells[iy * kW + ix] ? 1 : 0;
    return n;
}

int OccupancyGrid::count() const {
    int n = 0;
    for (auto c : cells) n += c ? 1 : 0;
    return n;
}

// ------------------------------------------------------------------ helpers

namespace {
float percentile(std::vector<float> &v, float q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(q * (v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}
float ema(float prev, float now, float a) { return prev + (now - prev) * a; }
V2 ema(V2 prev, V2 now, float a) { return {ema(prev.x, now.x, a), ema(prev.y, now.y, a)}; }

// Plano por mínimos quadrados de Y = aX + bZ + c (câmera), devolvido como n,d.
bool fitPlaneLS(const std::vector<V3> &pts, V3 &n, float &d) {
    double sxx = 0, sxz = 0, szz = 0, sx = 0, sz = 0, sxy = 0, szy = 0, sy = 0, cnt = 0;
    for (const auto &p : pts) {
        sxx += p.x * p.x; sxz += p.x * p.z; szz += p.z * p.z;
        sx += p.x; sz += p.z; sxy += p.x * p.y; szy += p.z * p.y; sy += p.y; cnt += 1;
    }
    if (cnt < 10) return false;
    // [sxx sxz sx; sxz szz sz; sx sz cnt] [a b c]^T = [sxy szy sy]
    const double A[3][3] = {{sxx, sxz, sx}, {sxz, szz, sz}, {sx, sz, cnt}};
    const double B[3] = {sxy, szy, sy};
    const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) -
                       A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                       A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (std::fabs(det) < 1e-12) return false;
    auto solve = [&](int col) {
        double M[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) M[i][j] = (j == col) ? B[i] : A[i][j];
        return (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det;
    };
    const double a = solve(0), b = solve(1), c = solve(2);
    const double len = std::sqrt(a * a + 1 + b * b);
    n = V3(static_cast<float>(a / len), static_cast<float>(-1 / len), static_cast<float>(b / len));
    d = static_cast<float>(c / len);
    return true;
}
} // namespace

void BodyTracker::reset() {
    tracks_.clear();
    frame_ = BodyFrame{};
    floorValid_ = false;
    frames_ = 0;
    lastTime_ = -1;
}

// ------------------------------------------------------------------- chão

void BodyTracker::estimateFloor(const std::vector<V3> &pts, const std::vector<uint8_t> &lowerHalf) {
    std::vector<V3> sample;
    sample.reserve(4000);
    for (size_t i = 0; i < pts.size(); ++i)
        if (lowerHalf[i] && pts[i].z > 0 && pts[i].z < 5.0f) sample.push_back(pts[i]);
    if (sample.size() < 200) return;

    auto rnd = [&]() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; };
    const float thr = p_.floorInlier;
    int bestCount = 0;
    V3 bestN; float bestD = 0;

    auto countInliers = [&](V3 n, float d) {
        int c = 0;
        for (const auto &p : sample) c += std::fabs(n.dot(p) + d) < thr ? 1 : 0;
        return c;
    };

    for (int it = 0; it < 90; ++it) {
        const V3 &a = sample[rnd() % sample.size()];
        const V3 &b = sample[rnd() % sample.size()];
        const V3 &c = sample[rnd() % sample.size()];
        V3 n = (b - a).cross(c - a);
        if (n.len() < 1e-6f) continue;
        n = n.norm();
        if (n.y > 0) n = n * -1.f;           // aponta para cima (Y da câmera é para baixo)
        if (-n.y < 0.70f) continue;          // chão inclinado demais (> ~45°)
        const float d = -n.dot(a);
        if (d < 0.15f || d > 2.8f) continue; // câmera precisa estar acima do chão
        const int cnt = countInliers(n, d);
        if (cnt > bestCount) { bestCount = cnt; bestN = n; bestD = d; }
    }
    if (bestCount < static_cast<int>(sample.size() * 0.08f) || bestCount < 150) return;

    std::vector<V3> in;
    for (const auto &p : sample) if (std::fabs(bestN.dot(p) + bestD) < thr) in.push_back(p);
    V3 n; float d;
    if (!fitPlaneLS(in, n, d)) { n = bestN; d = bestD; }

    if (floorValid_) {
        // Mantém o plano atual se ele ainda explica tão bem os pontos.
        const int cur = countInliers(n_, d_);
        const int cand = countInliers(n, d);
        if (cand < cur * 1.05f) return;
        const float ang = std::acos(std::min(1.f, std::fabs(n.dot(n_))));
        if (ang < 0.09f && std::fabs(d - d_) < 0.08f) {
            n = (n_ * 0.7f + n * 0.3f).norm();
            d = d_ * 0.7f + d * 0.3f;
        }
    }
    n_ = n;
    d_ = d;
    floorValid_ = true;
}

// ------------------------------------------------------------------ update

V3 BodyTracker::toPlay(const V3 &p) const {
    return V3(-p.dot(basisRight_), basisN_.dot(p) + basisD_, p.dot(basisFwd_));
}

const BodyFrame &BodyTracker::update(const uint16_t *depthMm, const Intrinsics &intr, double now,
                                     const ColorLookup *color, const ExternalBodies *ext) {
    const float dt = lastTime_ < 0 ? 1.f / 30.f : static_cast<float>(std::max(1e-3, now - lastTime_));
    lastTime_ = now;
    ++frames_;
    const int st = p_.step;
    gw_ = intr.width / st;
    gh_ = intr.height / st;
    const int n = gw_ * gh_;
    cam_.assign(n, V3{});
    std::vector<uint8_t> lower(n, 0);

    // 1. Pontos 3D na grade reduzida
    for (int j = 0; j < gh_; ++j) {
        for (int i = 0; i < gw_; ++i) {
            const int u = i * st + st / 2, v = j * st + st / 2;
            uint16_t mm = depthMm[v * intr.width + u];
            if (!mm) mm = depthMm[(v - 1) * intr.width + u - 1];
            if (!mm) continue;
            const float z = mm * 0.001f;
            if (z < p_.minZ || z > 6.0f) continue;
            cam_[j * gw_ + i] = V3((u - intr.cx) * z / intr.fx, (v - intr.cy) * z / intr.fy, z);
            lower[j * gw_ + i] = j > gh_ * 45 / 100;
        }
    }

    // 2. Chão (o NiTE2 também estima o chão; usamos o dele quando é plausível)
    bool extFloor = false;
    if (ext && ext->floorValid) {
        const V3 nn = ext->floorNormal.norm();
        const float d = -nn.dot(ext->floorPoint);
        if (-nn.y >= 0.70f && d > 0.15f && d < 2.8f) {
            if (floorValid_) { n_ = (n_ * 0.8f + nn * 0.2f).norm(); d_ = d_ * 0.8f + d * 0.2f; }
            else { n_ = nn; d_ = d; }
            floorValid_ = true;
            extFloor = true;
        }
    }
    if (!extFloor && (!floorValid_ || frames_ % p_.floorEveryFrames == 1)) estimateFloor(cam_, lower);
    V3 nrm = n_;
    float dd = d_;
    if (!floorValid_) { nrm = V3(0, -1, 0); dd = 0.8f; }  // suposição: sensor a ~80 cm, sem inclinação
    // Base do plano de jogo
    V3 right = (V3(1, 0, 0) - nrm * nrm.x).norm();
    V3 fwd = nrm.cross(right).norm();
    basisN_ = nrm; basisRight_ = right; basisFwd_ = fwd; basisD_ = dd;

    frame_.floorFound = floorValid_;
    frame_.floorNormal = nrm;
    frame_.floorD = dd;
    frame_.cameraHeight = dd;
    frame_.cameraPitchDeg = std::atan2(-fwd.y, fwd.z) * -57.2958f;
    frame_.time = now;
    frame_.frameSeq = frames_;
    frame_.simulated = false;
    frame_.skeletonSource = ext != nullptr;

    // 3. Pixels candidatos (acima do chão, dentro da faixa)
    pix_.assign(n, Pix{0, 0, 0, 0, 0});
    label_.assign(n, -1);
    labelsOut_.assign(n, 0);
    std::vector<uint8_t> cand(n, 0);
    for (int k = 0; k < n; ++k) {
        const V3 &p = cam_[k];
        if (p.z <= 0) continue;
        const float h = nrm.dot(p) + dd;
        if (h < p_.floorCut) { labelsOut_[k] = 1; continue; }
        labelsOut_[k] = 2;
        if (p.z > p_.maxZ || h > 2.6f) continue;
        Pix px;
        px.x = -p.dot(right);   // positivo = direita do jogador
        px.y = h;
        px.z = p.dot(fwd);
        px.u = k % gw_;
        px.v = k / gw_;
        pix_[k] = px;
        cand[k] = 1;
    }

    // 4. Componentes: rótulos do NiTE2 ou componentes conexos com continuidade de profundidade
    std::vector<Comp> comps;
    std::vector<int> stack;
    if (ext && ext->userMap) {
        std::vector<std::pair<int, int>> idToComp;  // (id NiTE, índice)
        for (int k = 0; k < n; ++k) {
            if (!cand[k]) continue;
            const int u = (k % gw_) * st + st / 2, v = (k / gw_) * st + st / 2;
            const int id = ext->userMap[v * intr.width + u];
            if (id <= 0) continue;
            int ci = -1;
            for (auto &pr : idToComp) if (pr.first == id) ci = pr.second;
            if (ci < 0) {
                ci = static_cast<int>(comps.size());
                idToComp.push_back({id, ci});
                comps.emplace_back();
                comps.back().extId = id;
            }
            Comp &c = comps[ci];
            label_[k] = ci;
            c.idx.push_back(k);
            const Pix &pp = pix_[k];
            c.minX = std::min(c.minX, pp.x); c.maxX = std::max(c.maxX, pp.x);
            c.minY = std::min(c.minY, pp.y); c.maxY = std::max(c.maxY, pp.y);
            c.sumZ += pp.z;
            const int gu = k % gw_, gv = k / gw_;
            if (gv == 0) c.touchTop = true;
            if (gv == gh_ - 1) c.touchBottom = true;
            if (gu == 0) c.touchLeft = true;
            if (gu == gw_ - 1) c.touchRight = true;
        }
    }
    for (int k = 0; k < n && !(ext && ext->userMap); ++k) {
        if (!cand[k] || label_[k] >= 0) continue;
        Comp c;
        const int id = static_cast<int>(comps.size());
        stack.assign(1, k);
        label_[k] = id;
        while (!stack.empty()) {
            const int q = stack.back();
            stack.pop_back();
            c.idx.push_back(q);
            const Pix &pp = pix_[q];
            c.minX = std::min(c.minX, pp.x); c.maxX = std::max(c.maxX, pp.x);
            c.minY = std::min(c.minY, pp.y); c.maxY = std::max(c.maxY, pp.y);
            c.sumZ += pp.z;
            const int u = q % gw_, v = q / gw_;
            if (v == 0) c.touchTop = true;
            if (v == gh_ - 1) c.touchBottom = true;
            if (u == 0) c.touchLeft = true;
            if (u == gw_ - 1) c.touchRight = true;
            const float zq = cam_[q].z;
            const float tol = 0.05f + 0.025f * zq;
            const int nb[4] = {u > 0 ? q - 1 : -1, u < gw_ - 1 ? q + 1 : -1, v > 0 ? q - gw_ : -1, v < gh_ - 1 ? q + gw_ : -1};
            for (int m : nb) {
                if (m < 0 || !cand[m] || label_[m] >= 0) continue;
                if (std::fabs(cam_[m].z - zq) > tol) continue;
                label_[m] = id;
                stack.push_back(m);
            }
        }
        comps.push_back(std::move(c));
    }

    // 5. Filtra componentes com forma de pessoa
    struct Cand { int comp; float x, z; float score; int extId; };
    std::vector<Cand> people;
    for (int ci = 0; ci < static_cast<int>(comps.size()); ++ci) {
        const Comp &c = comps[ci];
        if (c.extId > 0) {
            // usuário do NiTE2: já é uma pessoa; só descarta restos muito pequenos
            if (c.idx.size() < 25) continue;
            std::vector<float> xs;
            for (int q : c.idx) xs.push_back(pix_[q].x);
            const float z = c.sumZ / c.idx.size();
            const float mx = percentile(xs, 0.5f);
            people.push_back({ci, mx, z, -std::fabs(mx) * 0.6f - std::fabs(z - 2.3f) * 0.4f + 0.8f, c.extId});
            continue;
        }
        if (c.idx.size() < 40) continue;
        const float z = c.sumZ / c.idx.size();
        const float pixSize = st * z / intr.fx;
        const float area = c.idx.size() * pixSize * pixSize;
        const float height = c.maxY;
        const float width = c.maxX - c.minX;
        if (area < 0.18f || area > 3.2f) continue;
        if (height < 0.85f || height > 2.45f) continue;
        if (width < 0.15f || width > 2.4f) continue;
        if (c.minY > 0.75f) continue;  // flutuando: não é uma pessoa em pé ou pulando
        // altura proporcionalmente maior que a largura média
        if (area / height > 0.95f) continue;
        std::vector<float> xs;
        xs.reserve(c.idx.size());
        for (int q : c.idx) xs.push_back(pix_[q].x);
        const float mx = percentile(xs, 0.5f);
        const float score = -std::fabs(mx) * 0.6f - std::fabs(z - 2.3f) * 0.4f + std::min(area, 0.8f);
        people.push_back({ci, mx, z, score, 0});
    }
    std::sort(people.begin(), people.end(), [](const Cand &a, const Cand &b) { return a.score > b.score; });

    // 6. Associação com as trilhas existentes
    auto skeletonOf = [&](int extId) -> const ExternalUser * {
        if (!ext || extId <= 0) return nullptr;
        for (const auto &u : ext->users) if (u.id == extId && u.skeleton) return &u;
        return nullptr;
    };
    std::vector<int> used(people.size(), 0);
    // primeiro o mesmo usuário do NiTE2; depois, pela posição
    std::vector<int> match(tracks_.size(), -1);
    for (size_t ti = 0; ti < tracks_.size(); ++ti)
        for (size_t i = 0; i < people.size(); ++i)
            if (!used[i] && tracks_[ti].extId > 0 && people[i].extId == tracks_[ti].extId) { match[ti] = static_cast<int>(i); used[i] = 1; break; }
    for (size_t ti = 0; ti < tracks_.size(); ++ti) {
        Track &t = tracks_[ti];
        int best = match[ti];
        if (best < 0) {
            bool idStillSeen = false;
            for (auto &pc : people) if (t.extId > 0 && pc.extId == t.extId) idStillSeen = true;
            float bestD = 0.75f;
            for (size_t i = 0; i < people.size() && !idStillSeen; ++i) {
                if (used[i]) continue;
                const float d = std::hypot(people[i].x - t.x, (people[i].z - t.z) * 0.7f);
                if (d < bestD) { bestD = d; best = static_cast<int>(i); }
            }
            if (best >= 0) used[best] = 1;
        }
        if (best >= 0) {
            t.missing = 0;
            t.x = people[best].x;
            t.z = people[best].z;
            t.extId = people[best].extId;
            ++t.confirm;
            measure(t, comps[people[best].comp], now, dt, depthMm, intr, color, skeletonOf(t.extId));
            for (int q : comps[people[best].comp].idx) labelsOut_[q] = static_cast<uint8_t>(10 + std::max(0, t.slot));
        } else {
            ++t.missing;
        }
    }
    for (size_t i = 0; i < people.size(); ++i) {
        if (used[i]) continue;
        if (tracks_.size() >= 4) break;
        Track t;
        t.id = nextId_++;
        t.x = people[i].x;
        t.z = people[i].z;
        t.confirm = 1;
        t.extId = people[i].extId;
        t.firstSeen = now;
        measure(t, comps[people[i].comp], now, dt, depthMm, intr, color, skeletonOf(t.extId));
        tracks_.push_back(std::move(t));
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                 [&](const Track &t) { return t.missing > p_.keepMissingFrames || (t.slot < 0 && t.missing > 1); }),
                  tracks_.end());

    // 7. Vagas de jogador: trilhas confirmadas mantêm sua vaga
    bool slotUsed[BodyFrame::kMaxPlayers] = {false, false};
    for (auto &t : tracks_) if (t.slot >= 0) slotUsed[t.slot] = true;
    for (auto &t : tracks_) {
        if (t.slot >= 0 || t.confirm < p_.confirmFrames) continue;
        for (int s = 0; s < BodyFrame::kMaxPlayers; ++s)
            if (!slotUsed[s]) { t.slot = s; slotUsed[s] = true; break; }
    }
    // Ordena: o jogador 1 fica sempre à esquerda quando os dois entram juntos
    for (int s = 0; s < BodyFrame::kMaxPlayers; ++s) {
        PlayerBody &pb = frame_.players[s];
        const Track *tr = nullptr;
        for (auto &t : tracks_) if (t.slot == s) tr = &t;
        if (!tr || !tr->hasBody) { pb.present = false; pb.jumpEvent = false; pb.flapEvent = false; continue; }
        pb = tr->body;
        pb.present = true;
        if (tr->missing > 0) { pb.jumpEvent = false; pb.flapEvent = false; }
    }
    return frame_;
}

// --------------------------------------------------------------- medições

void BodyTracker::measure(Track &t, const Comp &c, double now, float dt, const uint16_t *depthMm,
                          const Intrinsics &intr, const ColorLookup *color, const ExternalUser *skel) {
    PlayerBody prev = t.body;
    const bool first = !t.hasBody;
    PlayerBody b = prev;
    b.trackId = t.id;
    b.seenSeconds = static_cast<float>(now - t.firstSeen);

    std::vector<float> xs, zs, ys;
    xs.reserve(c.idx.size());
    for (int q : c.idx) { xs.push_back(pix_[q].x); zs.push_back(pix_[q].z); }
    std::vector<float> xsMid;
    // faixa do tronco: 40%..75% da altura máxima do componente
    for (int q : c.idx) {
        const float y = pix_[q].y;
        if (y > c.maxY * 0.40f && y < c.maxY * 0.72f) xsMid.push_back(pix_[q].x);
    }
    const float torsoX = xsMid.size() > 10 ? percentile(xsMid, 0.5f) : percentile(xs, 0.5f);
    const float z = percentile(zs, 0.5f);

    // Grade de ocupação e silhueta
    b.grid.clear();
    const int SW = PlayerBody::kSilW, SH = PlayerBody::kSilH;
    b.silhouette.assign(SW * SH, 0);
    const bool wantColor = color != nullptr;
    if (wantColor) b.silhouetteRgb.assign(SW * SH * 3, 0); else b.silhouetteRgb.clear();
    const int st = p_.step;
    for (int q : c.idx) {
        const Pix &pp = pix_[q];
        b.grid.set(OccupancyGrid::cx(pp.x), OccupancyGrid::cy(pp.y));
        const int sx = static_cast<int>((pp.x + 2.0f) / PlayerBody::kSilCell);
        const int sy = static_cast<int>(pp.y / PlayerBody::kSilCell);
        uint8_t rgb[3] = {0, 0, 0};
        bool hasRgb = false;
        if (wantColor) {
            const int u = pp.u * st + st / 2, v = pp.v * st + st / 2;
            hasRgb = color->color(u, v, depthMm[v * intr.width + u], rgb);
        }
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const int X = sx + dx, Y = sy + dy;
                if (X < 0 || Y < 0 || X >= SW || Y >= SH) continue;
                b.silhouette[Y * SW + X] = 255;
                if (hasRgb) {
                    uint8_t *o = &b.silhouetteRgb[(Y * SW + X) * 3];
                    o[0] = rgb[0]; o[1] = rgb[1]; o[2] = rgb[2];
                }
            }
    }

    // Cabeça: primeira faixa (de cima para baixo) com largura de cabeça (>= 13 cm)
    // que continua em ombros largos logo abaixo. Mãos levantadas são estreitas e
    // não se ligam a ombros, por isso não contam como cabeça.
    float headY = c.maxY, headX = torsoX;
    {
        const int SW2 = PlayerBody::kSilW, SH2 = PlayerBody::kSilH;
        const float cell = PlayerBody::kSilCell;
        auto runsAt = [&](int iy, std::vector<std::pair<int, int>> &runs) {
            runs.clear();
            int startX = -1, gap = 0;
            for (int ix = 0; ix <= SW2; ++ix) {
                const bool on = ix < SW2 && b.silhouette[iy * SW2 + ix];
                if (on) { if (startX < 0) startX = ix; gap = 0; }
                else if (startX >= 0) {
                    if (++gap > 1 || ix == SW2) { runs.push_back({startX, ix - gap}); startX = -1; gap = 0; }
                }
            }
        };
        std::vector<std::pair<int, int>> runs, below;
        bool done = false;
        for (int iy = SH2 - 1; iy >= 0 && !done; --iy) {
            runsAt(iy, runs);
            for (auto &r : runs) {
                const float w = (r.second - r.first + 1) * cell;
                if (w < 0.13f) continue;
                // procura ombros (>= 30 cm) sobrepostos nos próximos 40 cm
                bool shoulders = false;
                for (int k = 1; k <= static_cast<int>(0.40f / cell) && iy - k >= 0 && !shoulders; ++k) {
                    runsAt(iy - k, below);
                    for (auto &r2 : below) {
                        const float w2 = (r2.second - r2.first + 1) * cell;
                        const float c1 = 0.5f * (r.first + r.second), c2 = 0.5f * (r2.first + r2.second);
                        if (w2 >= 0.30f && r2.first <= r.second && r2.second >= r.first &&
                            std::fabs(c1 - c2) * cell < 0.14f) { shoulders = true; break; }
                    }
                }
                if (!shoulders) continue;
                headY = (iy + 1) * cell;
                // centro da cabeça: média dos centros das faixas nos próximos 12 cm
                float sxh = 0; int nh = 0;
                int lo = r.first, hi = r.second;
                for (int k = 0; k <= static_cast<int>(0.12f / cell) && iy - k >= 0; ++k) {
                    runsAt(iy - k, below);
                    for (auto &r2 : below)
                        if (r2.first <= hi && r2.second >= lo && (r2.second - r2.first + 1) * cell < 0.30f) {
                            sxh += 0.5f * (r2.first + r2.second); ++nh; lo = r2.first; hi = r2.second; break;
                        }
                }
                headX = nh ? (sxh / nh + 0.5f) * cell - 2.0f : 0.5f * (r.first + r.second + 1) * cell - 2.0f;
                done = true;
                break;
            }
        }
    }
    std::vector<float> all;
    all.reserve(c.idx.size());
    for (int q : c.idx) all.push_back(pix_[q].y);
    const float bottomY = percentile(all, 0.03f);

    // Linha de base (últimos 3 s)
    while (!t.hist.empty() && now - t.hist.front().t > 3.0) t.hist.pop_front();
    float bottomBase = bottomY, standY = headY;
    if (!t.hist.empty()) {
        std::vector<float> bs;
        for (auto &h : t.hist) bs.push_back(h.bottomY);
        bottomBase = percentile(bs, 0.5f);
        standY = 0;
        for (auto &h : t.hist) if (h.bottomY < bottomBase + 0.06f) standY = std::max(standY, h.headY);
        if (standY <= 0) standY = prev.standY > 0 ? prev.standY : headY;
    }
    if (bottomY < bottomBase + 0.06f) standY = std::max(standY, headY);

    // Pulo
    const float clear = bottomY - bottomBase;
    const bool air = clear > 0.09f && headY > standY - 0.03f;
    b.jumpEvent = air && !t.airborne && now - t.lastJump > 0.3;
    if (b.jumpEvent) t.lastJump = now;
    t.airborne = air;
    b.airborne = air;
    b.jumpHeight = air ? clear : 0.f;

    // Agachar
    b.crouch = air ? 0.f : std::clamp((standY - headY) / (0.30f * standY), 0.f, 1.f);
    b.ducking = b.crouch > 0.45f;

    // Inclinação: do quadril até o centro da cabeça
    float lean = 0;
    {
        std::vector<float> hp;
        const float hipY = 0.52f * standY;
        for (int q : c.idx) {
            const float y = pix_[q].y;
            if (y > hipY - 0.06f && y < hipY + 0.06f && std::fabs(pix_[q].x - torsoX) < 0.35f) hp.push_back(pix_[q].x);
        }
        if (hp.size() > 4 && headY > hipY + 0.3f) {
            const float hipX = percentile(hp, 0.5f);
            lean = std::atan2(headX - hipX, (headY - 0.12f) - hipY);
        }
    }

    // Mãos: extremos laterais, com bônus para pontos acima dos ombros
    V2 hands[2];
    bool found[2] = {false, false};
    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? -1.f : 1.f;  // 0 = esquerda (x negativo)
        float best = -1;
        for (int q : c.idx) {
            const float x = pix_[q].x, y = pix_[q].y;
            if (y < 0.30f * standY) continue;
            const float lat = (x - torsoX) * sign;
            if (lat < 0.08f) continue;
            const float s = lat + 1.2f * std::max(0.f, y - 0.75f * standY);
            best = std::max(best, s);
        }
        if (best < 0) continue;
        float sx = 0, sy = 0; int cnt = 0;
        for (int q : c.idx) {
            const float x = pix_[q].x, y = pix_[q].y;
            if (y < 0.30f * standY) continue;
            const float lat = (x - torsoX) * sign;
            if (lat < 0.08f) continue;
            const float s = lat + 1.2f * std::max(0.f, y - 0.75f * standY);
            if (s >= best - 0.05f) { sx += x; sy += y; ++cnt; }
        }
        hands[side] = {sx / cnt, sy / cnt};
        found[side] = true;
    }
    for (int side = 0; side < 2; ++side)
        if (!found[side]) hands[side] = {torsoX + (side == 0 ? -0.22f : 0.22f), 0.48f * standY};

    // Esqueleto do NiTE2: mãos, centro da cabeça e inclinação vêm das articulações medidas
    b.skeleton = false;
    if (skel) {
        std::array<V3, PlayerBody::kJoints> pj;
        for (int j = 0; j < PlayerBody::kJoints; ++j) pj[j] = toPlay(skel->joints[j]);
        // Lado pelo ombro: a cadeia cujo ombro tem x menor é a esquerda do jogador
        // (independe de o NiTE nomear do ponto de vista da câmera ou do usuário).
        static const int chainA[6] = {2, 4, 6, 9, 11, 13}, chainB[6] = {3, 5, 7, 10, 12, 14};
        const bool swap = pj[3].x < pj[2].x;
        b.joints[0] = pj[0]; b.jointConf[0] = skel->conf[0];
        b.joints[1] = pj[1]; b.jointConf[1] = skel->conf[1];
        b.joints[8] = pj[8]; b.jointConf[8] = skel->conf[8];
        for (int k = 0; k < 6; ++k) {
            const int l = swap ? chainB[k] : chainA[k], r = swap ? chainA[k] : chainB[k];
            b.joints[chainA[k]] = pj[l]; b.jointConf[chainA[k]] = skel->conf[l];
            b.joints[chainB[k]] = pj[r]; b.jointConf[chainB[k]] = skel->conf[r];
        }
        const float *cf = b.jointConf.data();
        b.skeleton = cf[0] >= 0.5f && cf[8] >= 0.5f;
        if (b.skeleton) {
            if (cf[6] >= 0.5f) hands[0] = {b.joints[6].x, b.joints[6].y};
            if (cf[7] >= 0.5f) hands[1] = {b.joints[7].x, b.joints[7].y};
            headX = b.joints[0].x;
            if (cf[9] >= 0.5f && cf[10] >= 0.5f) {
                const V3 hip = (b.joints[9] + b.joints[10]) * 0.5f;
                if (b.joints[0].y > hip.y + 0.25f) lean = std::atan2(b.joints[0].x - hip.x, b.joints[0].y - hip.y);
            }
        }
    }

    const float a = first ? 1.f : 0.6f;
    b.x = first ? torsoX : ema(prev.x, torsoX, 0.6f);
    b.z = first ? z : ema(prev.z, z, 0.4f);
    b.headY = first ? headY : ema(prev.headY, headY, 0.7f);
    b.standY = standY;
    b.bottomY = bottomY;
    b.lean = first ? lean : ema(prev.lean, lean, 0.5f);
    const V2 lh = first ? hands[0] : ema(prev.leftHand, hands[0], a);
    const V2 rh = first ? hands[1] : ema(prev.rightHand, hands[1], a);
    if (!first) {
        b.leftHandVel = ema(prev.leftHandVel, (lh - prev.leftHand) * (1.f / dt), 0.5f);
        b.rightHandVel = ema(prev.rightHandVel, (rh - prev.rightHand) * (1.f / dt), 0.5f);
    }
    b.leftHand = lh;
    b.rightHand = rh;
    b.armsSpread = (rh - lh).len();
    b.handsUp = lh.y > headY + 0.02f && rh.y > headY + 0.02f;

    // Batida de braços: média das mãos cai rápido a partir de cima dos ombros
    const float armsY = 0.5f * (lh.y + rh.y);
    b.flapEvent = false;
    b.flapStrength = 0;
    float maxRecent = armsY;
    for (auto &h : t.hist) if (now - h.t < 0.35) maxRecent = std::max(maxRecent, h.armsY);
    const float drop = maxRecent - armsY;
    if (maxRecent > 0.68f * standY && drop > 0.30f && now - t.lastFlap > 0.25) {
        b.flapEvent = true;
        b.flapStrength = std::clamp(drop / 0.8f, 0.3f, 1.f);
        t.lastFlap = now;
        // zera o histórico de braços para não contar a mesma batida duas vezes
        for (auto &h : t.hist) h.armsY = armsY;
    }
    t.hist.push_back({now, headY, bottomY, armsY});

    // Topo visível da imagem na distância do jogador
    {
        const V3 dir(0, (0.5f - intr.cy) / intr.fy, 1);
        const float along = dir.dot(basisFwd_);
        if (along > 1e-3f) {
            const V3 top = dir * (z / along);
            b.visibleTopY = basisN_.dot(top) + basisD_;
        }
        // bordas laterais (x do jogo é o inverso do x da câmera)
        const float camLeft = -intr.cx / intr.fx * z, camRight = (intr.width - intr.cx) / intr.fx * z;
        b.visibleMinX = -camRight;
        b.visibleMaxX = -camLeft;
    }

    // Orientações
    b.guidance = G_NONE;
    if (z < 1.25f) b.guidance |= G_TOO_CLOSE;
    if (z > 3.8f) b.guidance |= G_TOO_FAR;
    if (c.touchTop) b.guidance |= G_HEAD_CUT;
    if (c.touchBottom && bottomY > 0.2f) b.guidance |= G_FEET_CUT;
    if (c.touchLeft) b.guidance |= G_RIGHT_EDGE;   // coluna esquerda da imagem = direita do jogador
    if (c.touchRight) b.guidance |= G_LEFT_EDGE;

    t.body = b;
    t.hasBody = true;
}

} // namespace k3d
