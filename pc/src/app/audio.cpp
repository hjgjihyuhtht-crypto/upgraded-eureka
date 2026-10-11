#include "app/audio.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>

namespace k3d {

using game::Sfx;

namespace {
constexpr int kRate = 44100;
constexpr float kPi = 3.14159265f;

struct Synth {
    std::vector<float> buf;
    uint32_t rng = 777;
    explicit Synth(float seconds) : buf(static_cast<size_t>(seconds * kRate), 0.f) {}
    float noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFF) / 32768.f - 1.f; }
    // tom com envelope exponencial
    void tone(float t0, float dur, float f0, float f1, float amp, float decay, int harmonics = 1) {
        float phase = 0;
        const size_t a = static_cast<size_t>(t0 * kRate), n = static_cast<size_t>(dur * kRate);
        for (size_t i = 0; i < n && a + i < buf.size(); ++i) {
            const float t = i / static_cast<float>(kRate);
            const float f = f0 + (f1 - f0) * (t / dur);
            phase += 2 * kPi * f / kRate;
            float v = 0;
            for (int h = 1; h <= harmonics; ++h) v += std::sin(phase * h) / h;
            const float env = std::exp(-t * decay) * std::min(1.f, t * 400.f);
            buf[a + i] += v * amp * env;
        }
    }
    void noiseBurst(float t0, float dur, float amp, float decay, float lowpass) {
        const size_t a = static_cast<size_t>(t0 * kRate), n = static_cast<size_t>(dur * kRate);
        float y = 0;
        for (size_t i = 0; i < n && a + i < buf.size(); ++i) {
            const float t = i / static_cast<float>(kRate);
            y += (noise() - y) * lowpass;
            buf[a + i] += y * amp * std::exp(-t * decay) * std::min(1.f, t * 300.f);
        }
    }
    Wave wave() const {
        Wave w{};
        w.frameCount = static_cast<unsigned int>(buf.size());
        w.sampleRate = kRate;
        w.sampleSize = 16;
        w.channels = 1;
        auto *d = static_cast<int16_t *>(MemAlloc(static_cast<unsigned int>(buf.size() * 2)));
        for (size_t i = 0; i < buf.size(); ++i) d[i] = static_cast<int16_t>(std::fmax(-1.f, std::fmin(1.f, buf[i])) * 30000);
        w.data = d;
        return w;
    }
};

Wave make(Sfx s) {
    switch (s) {
        case Sfx::Gem: { Synth y(0.35f); y.tone(0, 0.3f, 1320, 1320, 0.35f, 12, 2); y.tone(0.06f, 0.28f, 1760, 1760, 0.3f, 12, 2); return y.wave(); }
        case Sfx::Ring: { Synth y(0.7f); for (int i = 0; i < 4; ++i) y.tone(i * 0.08f, 0.4f, 880 * std::pow(1.26f, i), 880 * std::pow(1.26f, i), 0.25f, 6, 2); return y.wave(); }
        case Sfx::Bump: { Synth y(0.4f); y.tone(0, 0.35f, 140, 60, 0.7f, 10, 3); y.noiseBurst(0, 0.2f, 0.4f, 25, 0.2f); return y.wave(); }
        case Sfx::Splash: { Synth y(0.8f); y.noiseBurst(0, 0.75f, 0.55f, 5, 0.35f); return y.wave(); }
        case Sfx::Jump: { Synth y(0.35f); y.tone(0, 0.3f, 300, 700, 0.35f, 8, 2); return y.wave(); }
        case Sfx::Hit: { Synth y(0.25f); y.tone(0, 0.2f, 520, 300, 0.5f, 20, 3); y.noiseBurst(0, 0.05f, 0.3f, 60, 0.5f); return y.wave(); }
        case Sfx::Break: { Synth y(0.45f); y.noiseBurst(0, 0.4f, 0.5f, 12, 0.6f); y.tone(0, 0.15f, 900, 500, 0.25f, 25, 1); return y.wave(); }
        case Sfx::Whoosh: { Synth y(0.45f); y.noiseBurst(0, 0.42f, 0.35f, 6, 0.08f); return y.wave(); }
        case Sfx::Pop: { Synth y(0.15f); y.tone(0, 0.12f, 900, 1500, 0.45f, 35, 1); y.noiseBurst(0, 0.03f, 0.3f, 90, 0.7f); return y.wave(); }
        case Sfx::Crack: { Synth y(0.4f); for (int i = 0; i < 5; ++i) y.noiseBurst(i * 0.05f, 0.05f, 0.5f, 60, 0.8f); return y.wave(); }
        case Sfx::Plug: { Synth y(0.3f); y.tone(0, 0.25f, 400, 260, 0.5f, 14, 2); return y.wave(); }
        case Sfx::Countdown: { Synth y(0.25f); y.tone(0, 0.2f, 660, 660, 0.4f, 10, 1); return y.wave(); }
        case Sfx::Go: { Synth y(0.6f); y.tone(0, 0.55f, 990, 990, 0.45f, 4, 2); return y.wave(); }
        case Sfx::Finish:
        case Sfx::MedalSfx: {
            Synth y(1.4f);
            const float notes[] = {523, 659, 784, 1046};
            for (int i = 0; i < 4; ++i) y.tone(i * 0.14f, 0.9f, notes[i], notes[i], 0.22f, 3, 3);
            return y.wave();
        }
        case Sfx::Miss: { Synth y(0.45f); y.tone(0, 0.4f, 330, 180, 0.35f, 6, 2); return y.wave(); }
        case Sfx::Click: { Synth y(0.08f); y.tone(0, 0.06f, 1200, 1000, 0.3f, 60, 1); return y.wave(); }
        case Sfx::Hover: { Synth y(0.06f); y.tone(0, 0.05f, 1800, 1800, 0.12f, 80, 1); return y.wave(); }
        case Sfx::Flap: { Synth y(0.3f); y.noiseBurst(0, 0.25f, 0.3f, 14, 0.12f); y.tone(0, 0.2f, 220, 420, 0.15f, 12, 1); return y.wave(); }
        default: { Synth y(0.05f); return y.wave(); }
    }
}

// WAV PCM 16 bits mono em memória
std::vector<unsigned char> wavBytes(const std::vector<float> &x) {
    std::vector<unsigned char> b(44 + x.size() * 2);
    auto w32 = [&](size_t o, uint32_t v) { std::memcpy(&b[o], &v, 4); };
    auto w16 = [&](size_t o, uint16_t v) { std::memcpy(&b[o], &v, 2); };
    std::memcpy(&b[0], "RIFF", 4); w32(4, static_cast<uint32_t>(36 + x.size() * 2)); std::memcpy(&b[8], "WAVE", 4);
    std::memcpy(&b[12], "fmt ", 4); w32(16, 16); w16(20, 1); w16(22, 1); w32(24, kRate); w32(28, kRate * 2); w16(32, 2); w16(34, 16);
    std::memcpy(&b[36], "data", 4); w32(40, static_cast<uint32_t>(x.size() * 2));
    for (size_t i = 0; i < x.size(); ++i) {
        const int16_t v = static_cast<int16_t>(std::fmax(-1.f, std::fmin(1.f, x[i])) * 26000);
        std::memcpy(&b[44 + i * 2], &v, 2);
    }
    return b;
}

// Laço musical de 16 compassos: acordes suaves + arpejo, tom e andamento por "clima".
std::vector<float> composeLoop(int mood) {
    const float bpm = mood == 0 ? 92.f : 104.f + mood * 6.f;
    const float beat = 60.f / bpm;
    const int bars = 8;
    const float secs = bars * 4 * beat;
    Synth y(secs);
    const int roots[5][4] = {{0, 9, 5, 7}, {0, 5, 9, 7}, {2, 7, 0, 5}, {9, 5, 0, 7}, {0, 4, 9, 5}};
    const int *prog = roots[mood % 5];
    const float base = mood == 0 ? 196.f : 220.f;
    auto freq = [&](int semi) { return base * std::pow(2.f, semi / 12.f); };
    for (int bar = 0; bar < bars; ++bar) {
        const int r = prog[bar % 4];
        const float t0 = bar * 4 * beat;
        // acorde (pad)
        for (int k : {0, 4, 7}) y.tone(t0, 4 * beat, freq(r + k) * 0.5f, freq(r + k) * 0.5f, 0.05f, 0.6f, 2);
        // baixo
        y.tone(t0, 2 * beat, freq(r) * 0.25f, freq(r) * 0.25f, 0.12f, 1.5f, 3);
        y.tone(t0 + 2 * beat, 2 * beat, freq(r) * 0.25f, freq(r) * 0.25f, 0.1f, 1.5f, 3);
        // arpejo
        const int arp[8] = {0, 7, 12, 16, 12, 7, 4, 7};
        for (int i = 0; i < 8; ++i) y.tone(t0 + i * beat * 0.5f, beat * 0.45f, freq(r + arp[i]), freq(r + arp[i]), 0.06f, 7, 2);
        // percussão leve nos jogos
        if (mood > 0)
            for (int i = 0; i < 4; ++i) { y.noiseBurst(t0 + i * beat, 0.05f, 0.08f, 60, 0.9f); y.tone(t0 + i * beat, 0.12f, 120, 50, i % 2 ? 0.0f : 0.25f, 25, 1); }
    }
    return y.buf;
}
} // namespace

void Audio::init() {
    InitAudioDevice();
    ready_ = IsAudioDeviceReady();
    if (!ready_) return;
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
        Wave w = make(static_cast<Sfx>(i));
        sounds_[i] = LoadSoundFromWave(w);
        UnloadWave(w);
    }
}

void Audio::shutdown() {
    if (!ready_) return;
    if (musicLoaded_) { StopMusicStream(music_); UnloadMusicStream(music_); musicLoaded_ = false; }
    for (auto &s : sounds_) UnloadSound(s);
    CloseAudioDevice();
    ready_ = false;
}

void Audio::play(Sfx s, float volume) {
    if (!ready_) return;
    Sound &snd = sounds_[static_cast<int>(s)];
    SetSoundVolume(snd, volume * master);
    PlaySound(snd);
}

void Audio::playAll(const std::vector<Sfx> &list) {
    // evita empilhar o mesmo som várias vezes no mesmo quadro
    bool done[static_cast<int>(Sfx::Count)] = {};
    for (Sfx s : list) {
        if (done[static_cast<int>(s)]) continue;
        done[static_cast<int>(s)] = true;
        play(s);
    }
}

void Audio::music(int mood) {
    if (!ready_ || mood == mood_) return;
    mood_ = mood;
    if (musicLoaded_) { StopMusicStream(music_); UnloadMusicStream(music_); musicLoaded_ = false; }
    if (mood < 0) return;
    musicWav_ = wavBytes(composeLoop(mood));
    music_ = LoadMusicStreamFromMemory(".wav", musicWav_.data(), static_cast<int>(musicWav_.size()));
    music_.looping = true;
    SetMusicVolume(music_, 0.35f * master);
    PlayMusicStream(music_);
    musicLoaded_ = true;
}

void Audio::update() {
    if (ready_ && musicLoaded_) UpdateMusicStream(music_);
}

} // namespace k3d
