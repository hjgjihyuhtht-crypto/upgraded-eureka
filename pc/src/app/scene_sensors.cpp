// Ver sensores: profundidade, vídeo (RGB/IR), RGB alinhado, corpo rastreado,
// sonda de distância, captura de quadros, gravação e motor.
#include <algorithm>
#include <cmath>

#include "app/app.h"
#include <cstring>

#include "colorize.h"

namespace k3d {

namespace {

class SensorsScene : public Scene {
public:
    const char *name() const override { return "sensores"; }

    void enter(App &app) override {
        depthImg_.assign(640 * 480 * 4, 0);
        Image blank = GenImageColor(640, 488, BLACK);
        depthTex_ = LoadTextureFromImage(blank);
        videoTex_ = LoadTextureFromImage(blank);
        alignedTex_ = LoadTextureFromImage(blank);
        UnloadImage(blank);
        Image lb = GenImageColor(160, 120, BLACK);
        labelTex_ = LoadTextureFromImage(lb);
        UnloadImage(lb);
        app.audio.music(-1);
        // RGB/IR, gravação e motor usam o libfreenect: sai do NiTE2 enquanto esta tela está aberta
        if (app.usingNite()) {
            std::string err;
            if (!app.useFreenectForSensors(true, err)) { app.closeInput(); app.go(makeWaitScene()); return; }
            msg_ = "NiTE2 pausado: a tela de sensores usa o libfreenect.";
            msgT_ = 4.f;
        } else {
            std::string err;
            app.useFreenectForSensors(true, err);
        }
    }

    void leave(App &app) override {
        if (FrameSource *src = app.frameSource()) src->stopRecording();
        for (Texture2D *t : {&depthTex_, &videoTex_, &alignedTex_, &labelTex_}) UnloadTexture(*t);
        if (app.mode() == InputMode::Kinect && ir_) app.streamVideoIr(false);
        app.audio.music(0);
        std::string err;
        if (!app.useFreenectForSensors(false, err)) { app.closeInput(); app.go(makeWaitScene()); }
    }

    void update(App &app, float dt) override {
        if (app.ui.backPressed()) { app.go(makeMainMenu()); return; }
        msgT_ = std::max(0.f, msgT_ - dt);
        FrameSource *src = app.frameSource();
        DepthViewParams dp;
        dp.minMm = minMm_;
        dp.maxMm = maxMm_;
        dp.colormap = turbo_ ? Colormap::Turbo : Colormap::Gray;
        dp.filter = static_cast<DepthFilter>(filter_);
        const VideoViewParams vp{0.f, 1.f};
        // profundidade
        bool depthDone = false;
        if (src) {
            const uint64_t seq = src->renderDepth(dp, depthImg_.data(), 640, 480, 640 * 4, depthSeq_);
            if (seq) { depthSeq_ = seq; UpdateTextureRec(depthTex_, {0, 0, 640, 480}, depthImg_.data()); }
            depthDone = true;
            // vídeo
            const FrameSource &cs = *src;
            double st[S_COUNT];
            cs.readStats(st);
            videoW_ = static_cast<int>(st[S_VIDEO_W]);
            videoH_ = static_cast<int>(st[S_VIDEO_H]);
            if (videoW_ > 0 && videoH_ > 0 && videoW_ <= 640 && videoH_ <= 488) {
                videoImg_.resize(static_cast<size_t>(videoW_) * videoH_ * 4);
                const uint64_t vs = src->renderVideo(vp, videoImg_.data(), videoW_, videoH_, videoW_ * 4, videoSeq_);
                if (vs) { videoSeq_ = vs; UpdateTextureRec(videoTex_, {0, 0, static_cast<float>(videoW_), static_cast<float>(videoH_)}, videoImg_.data()); }
            }
            if (aligned_) {
                alignedImg_.resize(640 * 480 * 4);
                const uint64_t as = src->renderAligned(vp, alignedImg_.data(), 640, 480, 640 * 4, alignedSeq_);
                if (as) { alignedSeq_ = as; UpdateTextureRec(alignedTex_, {0, 0, 640, 480}, alignedImg_.data()); }
            }
        }
        if (!depthDone && app.depthCount() != lastSimCount_ && !app.depthMm().empty()) {
            // simulação: colore a profundidade sintética diretamente
            lastSimCount_ = app.depthCount();
            std::vector<uint16_t> filt(app.depthMm().size());
            filterDepthMm(app.depthMm().data(), 640, 480, dp.filter, filt.data());
            depthMmToRgba(filt.data(), 640, 480, dp, depthImg_.data(), 640 * 4);
            UpdateTextureRec(depthTex_, {0, 0, 640, 480}, depthImg_.data());
        }
        // rótulos do rastreador
        const auto &lab = app.tracker().labelImage();
        if (!lab.empty()) {
            labelImg_.resize(lab.size() * 4);
            for (size_t i = 0; i < lab.size(); ++i) {
                Color c = BLACK;
                if (lab[i] == 1) c = Color{30, 70, 40, 255};
                else if (lab[i] == 2) c = Color{70, 74, 84, 255};
                else if (lab[i] == 10) c = theme::player1;
                else if (lab[i] == 11) c = theme::player2;
                std::memcpy(&labelImg_[i * 4], &c, 4);
            }
            if (app.tracker().labelWidth() == 160) UpdateTexture(labelTex_, labelImg_.data());
        }
    }

    // Converte um ponto da tela para o pixel da profundidade (ou -1)
    bool screenToDepth(Rectangle view, Vector2 m, int &x, int &y) {
        if (!CheckCollisionPointRec(m, view)) return false;
        x = static_cast<int>((m.x - view.x) / view.width * 640);
        y = static_cast<int>((m.y - view.y) / view.height * 480);
        return true;
    }

    void probe(App &app, int x, int y) {
        probeX_ = x; probeY_ = y;
        if (FrameSource *src = app.frameSource()) { src->probe(x, y, probe_); return; }
        // simulação
        const auto &mm = app.depthMm();
        for (double &v : probe_) v = 0;
        if (mm.empty()) return;
        const uint16_t d = mm[y * 640 + x];
        probe_[2] = d;
        if (!d) return;
        const Intrinsics &in = app.intrinsics();
        probe_[0] = 1;
        probe_[3] = (x - in.cx) * d / in.fx;
        probe_[4] = (y - in.cy) * d / in.fy;
        probe_[5] = d;
    }

    void say(const std::string &m) { msg_ = m; msgT_ = 4; }

    void draw(App &app) override {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.background(theme::bg, Color{6, 12, 22, 255});
        ui.textShadow("Sensores", 24 * s, 12 * s, 36 * s, theme::text);
        ui.text(app.modeLabel(), 200 * s, 24 * s, 20 * s, app.simulated() ? theme::yellow : theme::teal, Align::Left, true);

        // 2x2: profundidade | vídeo  /  corpo | painel
        const float top = 64 * s, pad = 12 * s;
        const float vw = (ui.w() - 3 * pad) * 0.5f, vh = std::min(vw * 0.75f, (ui.h() - top - 2 * pad) * 0.5f);
        const float vw2 = vh / 0.75f;
        const float x0 = (ui.w() - 2 * vw2 - pad) / 2;
        const Rectangle dv{x0, top, vw2, vh}, vv{x0 + pad + vw2, top, vw2, vh};
        const Rectangle bv{pad, top + vh + pad, ui.w() * 0.42f, ui.h() - top - vh - 2 * pad};
        const Rectangle pv{bv.x + bv.width + pad, top + vh + pad, ui.w() - 3 * pad - bv.width, ui.h() - top - vh - 2 * pad};

        // profundidade
        DrawTexturePro(depthTex_, {0, 0, 640, 480}, dv, {0, 0}, 0, WHITE);
        label(ui, dv, std::string("Profundidade ") + std::to_string(minMm_) + "–" + std::to_string(maxMm_) + " mm");
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int x, y;
            if (screenToDepth(dv, GetMousePosition(), x, y)) probe(app, x, y);
        }
        if (probeX_ >= 0) {
            const Vector2 c{dv.x + probeX_ * dv.width / 640, dv.y + probeY_ * dv.height / 480};
            DrawCircleLinesV(c, 10 * s, WHITE);
            DrawCircleLinesV(c, 12 * s, BLACK);
        }
        // vídeo / alinhado
        if (aligned_) {
            DrawTexturePro(alignedTex_, {0, 0, 640, 480}, vv, {0, 0}, 0, WHITE);
            label(ui, vv, "RGB alinhado à profundidade");
        } else if (videoW_ > 0) {
            DrawTexturePro(videoTex_, {0, 0, static_cast<float>(videoW_), static_cast<float>(videoH_)}, vv, {0, 0}, 0, WHITE);
            label(ui, vv, ir_ ? "Infravermelho (8 bits)" : "Câmera RGB");
        } else {
            DrawRectangleRec(vv, Color{0, 0, 0, 160});
            ui.textBox(app.mode() == InputMode::Simulation ? "Na simulação não há câmera de vídeo: só a profundidade sintética do boneco."
                                                           : "Aguardando vídeo...",
                       {vv.x + 20 * s, vv.y + vv.height / 2 - 20 * s, vv.width - 40 * s, 80 * s}, 20 * s, theme::muted, Align::Center);
        }
        // corpo no plano de jogo
        drawBody(app, bv);
        // painel
        drawPanel(app, pv);
        if (msgT_ > 0) {
            const float wv = ui.textWidth(msg_, 20 * s, true) + 30 * s;
            ui.panel({ui.w() / 2.f - wv / 2, ui.h() - 60 * s, wv, 44 * s}, Color{20, 120, 110, 240}, 0.5f);
            ui.text(msg_, ui.w() / 2.f, ui.h() - 50 * s, 20 * s, WHITE, Align::Center, true);
        }
    }

    void label(Ui &ui, Rectangle r, const std::string &t) {
        const float s = ui.s();
        DrawRectangleRounded({r.x + 6 * s, r.y + 6 * s, ui.textWidth(t, 16 * s, true) + 16 * s, 26 * s}, 0.4f, 4, Color{0, 0, 0, 160});
        ui.text(t, r.x + 14 * s, r.y + 9 * s, 16 * s, WHITE, Align::Left, true);
    }

    void drawBody(App &app, Rectangle r) {
        Ui &ui = app.ui;
        const float s = ui.s();
        DrawRectangleRec(r, Color{8, 16, 28, 255});
        // plano 4 m x 2,6 m, mantendo proporção
        const float k = std::min(r.width / 4.0f, r.height / 2.6f);
        const Rectangle pl{r.x + (r.width - 4 * k) / 2, r.y + (r.height - 2.6f * k), 4 * k, 2.6f * k};
        for (int m = 0; m <= 4; ++m) DrawLineV({pl.x + m * k, pl.y}, {pl.x + m * k, pl.y + pl.height}, Color{40, 60, 80, 255});
        for (int m = 0; m <= 2; ++m) DrawLineV({pl.x, pl.y + pl.height - m * k}, {pl.x + pl.width, pl.y + pl.height - m * k}, Color{40, 60, 80, 255});
        for (int i = 0; i < 2; ++i) app.silhouettes.draw2D(i, pl, 1.f);
        auto P = [&](V2 g) { return Vector2{pl.x + (g.x + 2) * k, pl.y + pl.height - g.y * k}; };
        const BodyFrame &b = app.body();
        for (int i = 0; i < 2; ++i) {
            const PlayerBody &p = b.players[i];
            if (!p.present) continue;
            const Color c = i == 0 ? theme::player1 : theme::player2;
            DrawCircleV(P(p.leftHand), 8 * s, theme::yellow);
            DrawCircleV(P(p.rightHand), 8 * s, theme::orange);
            DrawLineEx(P({p.x, 0.52f * p.standY}), P({p.x + std::sin(p.lean) * 0.6f, 0.52f * p.standY + std::cos(p.lean) * 0.6f}), 3 * s, WHITE);
            DrawLineEx(P({p.x - 0.3f, p.headY}), P({p.x + 0.3f, p.headY}), 2 * s, c);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "J%d  x %.2f m · %.2f m do sensor · altura %.2f m%s%s%s", i + 1, p.x, p.z, p.standY,
                          p.airborne ? " · PULANDO" : "", p.ducking ? " · ABAIXADO" : "", p.handsUp ? " · MÃOS PARA O ALTO" : "");
            ui.text(buf, r.x + 10 * s, r.y + 8 * s + i * 22 * s, 16 * s, c, Align::Left, true);
        }
        if (b.count() == 0) ui.text("Ninguém detectado", r.x + 10 * s, r.y + 8 * s, 16 * s, theme::muted, Align::Left, true);
        // rótulos (pequeno): chão, outros objetos e jogadores
        const Rectangle inset{r.x + r.width - 128 * s, r.y + r.height - 120 * s, 120 * s, 90 * s};
        DrawTexturePro(labelTex_, {0, 0, 160, 120}, inset, {0, 0}, 0, WHITE);
        DrawRectangleLinesEx(inset, 1, Color{90, 110, 140, 255});
        ui.text(b.floorFound ? "chão encontrado" : "chão NÃO encontrado", inset.x, inset.y - 18 * s, 14 * s,
                b.floorFound ? theme::teal : theme::pink);
        label(ui, {r.x, r.y + r.height - 40 * s, 0, 0}, "Corpo rastreado (plano de jogo, grade de 1 m)");
    }

    void drawPanel(App &app, Rectangle r) {
        Ui &ui = app.ui;
        const float s = ui.s();
        ui.panel(r, Color{18, 34, 56, 240}, 0.05f);
        FrameSource *src = app.frameSource();
        float y = r.y + 10 * s;
        const float x = r.x + 14 * s;
        auto line = [&](const std::string &t, Color c = theme::text) { ui.text(t, x, y, 17 * s, c); y += 24 * s; };
        char buf[200];
        if (src) {
            double st[S_COUNT];
            src->readStats(st);
            std::snprintf(buf, sizeof(buf), "Profundidade %.1f fps · %.0f%% válidos · %.0f–%.0f mm", st[S_DEPTH_FPS],
                          st[S_DEPTH_VALID_RATIO] * 100, st[S_DEPTH_MIN_MM], st[S_DEPTH_MAX_MM]);
            line(buf);
            std::snprintf(buf, sizeof(buf), "Vídeo %.1f fps · diferença entre fluxos %.1f ms", st[S_VIDEO_FPS], std::max(0.0, st[S_SYNC_DELTA_MS]));
            line(buf);
            std::snprintf(buf, sizeof(buf), "Centro da imagem: %.0f mm", st[S_DEPTH_CENTER_MM]);
            line(buf);
            if (st[S_REC_ACTIVE] > 0.5) {
                std::snprintf(buf, sizeof(buf), "● GRAVANDO · %.0f quadros · %.0f MB%s", st[S_REC_FRAMES], st[S_REC_BYTES] / 1e6,
                              st[S_REC_DROPPED] > 0 ? " · quadros descartados!" : "");
                line(buf, theme::pink);
            }
        } else {
            line("Simulação: profundidade sintética do boneco (sem sensor).", theme::yellow);
        }
        if (probe_[0] > 0.5)
            std::snprintf(buf, sizeof(buf), "Ponto (%d, %d): %.0f mm · X %.0f Y %.0f Z %.0f mm", probeX_, probeY_, probe_[2], probe_[3], probe_[4], probe_[5]);
        else if (probeX_ >= 0)
            std::snprintf(buf, sizeof(buf), "Ponto (%d, %d): sem medida", probeX_, probeY_);
        else
            std::snprintf(buf, sizeof(buf), "Clique na profundidade para medir a distância de um ponto.");
        line(buf, theme::yellow);

        // botões
        const float bh = 46 * s, bw = (r.width - 28 * s - 2 * 8 * s) / 3;
        float bx = x, by = y + 6 * s;
        auto next = [&]() { bx += bw + 8 * s; if (bx + bw > r.x + r.width) { bx = x; by += bh + 8 * s; } };
        if (ui.button("s_cmap", {bx, by, bw, bh}, turbo_ ? "Cores: Turbo" : "Cores: Cinza", BtnStyle::Secondary)) turbo_ = !turbo_;
        next();
        const char *fn[] = {"Filtro: nenhum", "Filtro: mediana", "Filtro: bordas", "Filtro: ambos"};
        if (ui.button("s_filter", {bx, by, bw, bh}, fn[filter_], BtnStyle::Secondary)) filter_ = (filter_ + 1) % 4;
        next();
        if (ui.button("s_range", {bx, by, bw, bh}, "Faixa: " + std::to_string(maxMm_ / 1000) + " m", BtnStyle::Secondary))
            maxMm_ = maxMm_ >= 8000 ? 2500 : maxMm_ + 1500;
        next();
        const bool live = app.mode() == InputMode::Kinect;
        if (ui.button("s_ir", {bx, by, bw, bh}, ir_ ? "Ver RGB" : "Ver infravermelho", BtnStyle::Secondary, live)) {
            ir_ = !ir_;
            aligned_ = false;
            app.streamVideoIr(ir_);
            videoSeq_ = 0;
        }
        next();
        if (ui.button("s_aligned", {bx, by, bw, bh}, aligned_ ? "Vídeo normal" : "RGB alinhado", BtnStyle::Secondary, src && !ir_))
            aligned_ = !aligned_;
        next();
        if (ui.button("s_capture", {bx, by, bw, bh}, "Capturar quadro", BtnStyle::Primary, src != nullptr)) capture(app);
        next();
        const bool rec = src && [&] { double st[S_COUNT]; src->readStats(st); return st[S_REC_ACTIVE] > 0.5; }();
        if (ui.button("s_rec", {bx, by, bw, bh}, rec ? "Parar gravação" : "Gravar", rec ? BtnStyle::Danger : BtnStyle::Secondary, live)) {
            if (rec) { src->stopRecording(); say("Gravação salva em " + app.paths.recordings); }
            else {
                const std::string path = app.paths.recordings + "/REC_" + nowStamp() + ".k3drec";
                std::string err;
                const std::string json = "{\"app\":\"Kinect 3D Studio PC\",\"modelo\":\"" + app.watcher.status().modelName() +
                                         "\",\"video\":\"" + (ir_ ? "IR" : "RGB") + "\"}";
                if (src->startRecording(path, json, true, true, err)) say("Gravando: as imagens estão sendo salvas neste computador");
                else say("Não foi possível gravar: " + err);
            }
        }
        next();
        PcKinectSession *k = app.kinect();
        const bool motor = k && k->hasMotor();
        if (ui.button("s_up", {bx, by, bw, bh}, "Inclinar para cima", BtnStyle::Secondary, motor)) { tilt_ = std::min(27.0, tilt_ + 5); k->setTilt(tilt_); }
        next();
        if (ui.button("s_down", {bx, by, bw, bh}, "Inclinar para baixo", BtnStyle::Secondary, motor)) { tilt_ = std::max(-27.0, tilt_ - 5); k->setTilt(tilt_); }
        next();
        if (app.mode() == InputMode::Recording && app.playback()) {
            if (ui.button("s_pause", {bx, by, bw, bh}, paused_ ? "Continuar" : "Pausar", BtnStyle::Secondary)) {
                paused_ = !paused_;
                app.playback()->setPaused(paused_);
            }
            next();
        }
        if (ui.button("s_back", {r.x + r.width - 170 * s, r.y + r.height - 56 * s, 156 * s, 46 * s}, "Voltar", BtnStyle::Secondary))
            app.go(makeMainMenu());
        if (!motor && live)
            ui.text("Motor indisponível neste modelo (só o 1414 tem motor acessível).", x, r.y + r.height - 40 * s, 15 * s, theme::muted);
    }

    void capture(App &app) {
        FrameSource *src = app.frameSource();
        if (!src) return;
        const std::string dir = app.paths.captures + "/CAP_" + nowStamp();
        makeDirs(dir);
        std::string err;
        const std::string json = src->capture(dir, static_cast<DepthFilter>(filter_), err);
        if (json.empty()) { say("Falha na captura: " + err); return; }
        const std::string meta = "{\"app\":\"Kinect 3D Studio PC\",\"fonte\":\"" + std::string(app.modeLabel()) + "\",\"captura\":" + json + "}";
        writeFileUtf8(dir + "/metadata.json", meta.data(), meta.size());
        say("Captura salva em " + dir);
        app.audio.play(game::Sfx::Click);
    }

private:
    Texture2D depthTex_{}, videoTex_{}, alignedTex_{}, labelTex_{};
    std::vector<unsigned char> depthImg_, videoImg_, alignedImg_, labelImg_;
    uint64_t depthSeq_ = 0, videoSeq_ = 0, alignedSeq_ = 0, lastSimCount_ = 0;
    int videoW_ = 0, videoH_ = 0;
    int minMm_ = 500, maxMm_ = 4500, filter_ = 0;
    bool turbo_ = true, ir_ = false, aligned_ = false, paused_ = false;
    int probeX_ = -1, probeY_ = -1;
    double probe_[6] = {0};
    double tilt_ = 0;
    std::string msg_;
    float msgT_ = 0;
};

} // namespace

std::unique_ptr<Scene> makeSensors() { return std::make_unique<SensorsScene>(); }

} // namespace k3d
