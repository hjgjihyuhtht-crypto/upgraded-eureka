#pragma once
// Sons gerados por código (nenhum arquivo de áudio externo) e música ambiente simples.
#include <vector>

#include "raylib.h"

#include "game/game_common.h"

namespace k3d {

class Audio {
public:
    void init();
    void shutdown();
    void play(game::Sfx s, float volume = 1.f);
    void playAll(const std::vector<game::Sfx> &list);
    void music(int mood);      // -1 desliga; 0 menu; 1..5 jogos
    void update();
    bool ready() const { return ready_; }
    float master = 0.8f;

private:
    bool ready_ = false;
    Sound sounds_[static_cast<int>(game::Sfx::Count)]{};
    Music music_{};
    bool musicLoaded_ = false;
    int mood_ = -2;
    std::vector<unsigned char> musicWav_;
};

} // namespace k3d
