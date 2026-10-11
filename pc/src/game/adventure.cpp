#include "game/adventure.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace k3d::game {

const std::vector<Adventure> &adventures() {
    static const std::vector<Adventure> v = {
        {"Correnteza Mansa", GameId::River, 0, 11},
        {"Primeiro Rebote", GameId::Rebound, 0, 12},
        {"Aquário Tranquilo", GameId::Leaks, 0, 13},
        {"Trilha das Nuvens", GameId::Dodge, 0, 21},
        {"Bolhas Flutuantes", GameId::Bubbles, 0, 22},
        {"Cânion das Pedras", GameId::River, 1, 23},
        {"Muralha Colorida", GameId::Rebound, 1, 31},
        {"Pressão Crescente", GameId::Leaks, 1, 32},
        {"Ponte dos Ventos", GameId::Dodge, 1, 33},
        {"Órbita Agitada", GameId::Bubbles, 1, 41},
        {"Cachoeira do Trovão", GameId::River, 2, 42},
        {"Tempestade de Bolas", GameId::Rebound, 2, 43},
        {"Fundo do Mar", GameId::Leaks, 2, 51},
        {"Desafio do Pico", GameId::Dodge, 2, 52},
        {"Chuva de Estrelas", GameId::Bubbles, 2, 53},
    };
    return v;
}

const std::vector<Expedition> &expeditions() {
    static const std::vector<Expedition> v = {
        {"Expedição 1 — Primeiros Passos", {0, 1, 2}},
        {"Expedição 2 — Ventos Novos", {3, 4, 5}},
        {"Expedição 3 — Coragem", {6, 7, 8}},
        {"Expedição 4 — Alta Velocidade", {9, 10, 11}},
        {"Expedição 5 — Lendas", {12, 13, 14}},
    };
    return v;
}

const std::vector<Emblem> &emblems() {
    static const std::vector<Emblem> v = {
        {"primeira", "Primeiros Passos", "Termine qualquer partida."},
        {"exp1", "Expedição Iniciada", "Ganhe medalha nas três aventuras da Expedição 1."},
        {"ouro", "Brilho Dourado", "Ganhe ouro em qualquer aventura."},
        {"platina", "Lenda", "Ganhe platina em qualquer aventura."},
        {"todas", "Explorador Completo", "Ganhe medalha nas 15 aventuras."},
        {"todas_ouro", "Mestre da Expedição", "Ganhe ouro ou platina nas 15 aventuras."},
        {"rio_limpo", "Navegador Perfeito", "Termine a Corredeira sem bater em pedras."},
        {"anel", "Voo pelo Anel", "Passe por um anel na Corredeira."},
        {"triplo", "Triplo", "Quebre três blocos com a mesma bola no Rebote."},
        {"paredao", "Paredão Limpo", "Quebre todos os blocos do Rebote."},
        {"intocavel", "Intocável", "Termine a Esquiva sem nenhuma colisão."},
        {"mola", "Mola", "Pule 50 vezes no total."},
        {"seco", "Nem uma Gota", "Termine o Aquário com a água abaixo de 20%."},
        {"maos_pes", "Mãos e Pés", "Tape três vazamentos quase ao mesmo tempo."},
        {"estoura", "Estoura-Tudo", "Estoure cinco bolhas em dois segundos."},
        {"astronauta", "Astronauta", "Flutue acima de 4 metros na Gravidade Zero."},
        {"dupla", "Trabalho em Equipe", "Termine uma partida com dois jogadores."},
        {"colecionador", "Colecionador", "Some 500 pontos de aventura."},
        {"maratona", "Maratonista", "Jogue por 30 minutos no total."},
        {"fotografo", "Fotógrafo", "Tire 20 fotos durante as partidas."},
    };
    return v;
}

Profile::Profile() {
    bestMedal.assign(adventures().size(), 0);
    bestScore.assign(adventures().size(), 0);
}

bool Profile::expeditionUnlocked(int e) const {
    if (e <= 0) return true;
    for (int a : expeditions()[e - 1].adventures)
        if (bestMedal[a] < static_cast<int>(Medal::Bronze)) return false;
    return expeditionUnlocked(e - 1);
}

int Profile::medalsCount(Medal atLeast) const {
    int n = 0;
    for (int m : bestMedal) n += m >= static_cast<int>(atLeast);
    return n;
}

bool Profile::hasEmblem(const std::string &id) const {
    return std::find(emblemsEarned.begin(), emblemsEarned.end(), id) != emblemsEarned.end();
}

std::vector<const Emblem *> Profile::record(const Result &r, int adv) {
    ++gamesPlayed;
    secondsPlayed += r.seconds;
    totalPoints += r.score;
    totalJumps += static_cast<int>(r.stat("jumps"));
    if (r.playersSeen >= 2) ++duoGames;
    if (adv >= 0 && adv < static_cast<int>(bestMedal.size())) {
        bestMedal[adv] = std::max(bestMedal[adv], static_cast<int>(r.medal));
        bestScore[adv] = std::max(bestScore[adv], r.score);
    } else {
        int &b = freeBest[static_cast<int>(r.game)][std::clamp(r.difficulty, 0, 2)];
        b = std::max(b, r.score);
    }
    return checkEmblems(&r);
}

std::vector<const Emblem *> Profile::recordPhotos(int n) {
    photosTaken += n;
    return checkEmblems(nullptr);
}

std::vector<const Emblem *> Profile::checkEmblems(const Result *r) {
    std::vector<const Emblem *> out;
    auto award = [&](const char *id, bool cond) {
        if (!cond || hasEmblem(id)) return;
        emblemsEarned.push_back(id);
        for (auto &e : emblems()) if (std::string(e.id) == id) out.push_back(&e);
    };
    award("primeira", gamesPlayed > 0);
    bool exp1 = true;
    for (int a : expeditions()[0].adventures) exp1 &= bestMedal[a] >= static_cast<int>(Medal::Bronze);
    award("exp1", exp1);
    award("ouro", medalsCount(Medal::Gold) > 0);
    award("platina", medalsCount(Medal::Platinum) > 0);
    award("todas", medalsCount(Medal::Bronze) == static_cast<int>(bestMedal.size()));
    award("todas_ouro", medalsCount(Medal::Gold) == static_cast<int>(bestMedal.size()));
    if (r) {
        const bool finished = r->seconds > 5;
        auto has = [&](const char *k) { return r->stats.count(k) > 0; };
        award("rio_limpo", r->game == GameId::River && finished && has("rockHits") && r->stat("rockHits") == 0 &&
                               r->score > 0);
        award("anel", r->game == GameId::River && r->stat("rings") > 0);
        award("triplo", r->game == GameId::Rebound && r->stat("maxTargetsOneBall") >= 3);
        award("paredao", r->game == GameId::Rebound && r->stat("cleared") > 0);
        award("intocavel", r->game == GameId::Dodge && finished && has("collisions") && r->stat("collisions") == 0 && r->score > 0);
        award("seco", r->game == GameId::Leaks && finished && has("maxWaterFrac") && r->stat("maxWaterFrac") < 0.2f && r->score > 0);
        award("maos_pes", r->game == GameId::Leaks && r->stat("maxSimultaneousSeals") >= 3);
        award("estoura", r->game == GameId::Bubbles && r->stat("maxCombo") >= 5);
        award("astronauta", r->game == GameId::Bubbles && r->stat("maxFloat") > 4.0f);
        award("dupla", r->playersSeen >= 2);
    }
    award("mola", totalJumps >= 50);
    award("colecionador", totalPoints >= 500);
    award("maratona", secondsPlayed >= 1800);
    award("fotografo", photosTaken >= 20);
    return out;
}

// Formato de texto simples "chave=valor", uma por linha.
bool Profile::save(const std::string &path) const {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(std::filesystem::u8path(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << "versao=1\n";
        f << "nome=" << name << "\n";
        f << "segundos=" << secondsPlayed << "\n";
        f << "partidas=" << gamesPlayed << "\n";
        f << "pontos=" << totalPoints << "\n";
        f << "pulos=" << totalJumps << "\n";
        f << "fotos=" << photosTaken << "\n";
        f << "duplas=" << duoGames << "\n";
        for (size_t i = 0; i < bestMedal.size(); ++i) f << "aventura." << i << "=" << bestMedal[i] << "," << bestScore[i] << "\n";
        for (int g = 0; g < kGameCount; ++g)
            for (int d = 0; d < 3; ++d) f << "livre." << g << "." << d << "=" << freeBest[g][d] << "\n";
        for (auto &e : emblemsEarned) f << "emblema=" << e << "\n";
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);
    return !ec;
}

bool Profile::load(const std::string &path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    Profile p;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        try {
            if (k == "nome") p.name = v;
            else if (k == "segundos") p.secondsPlayed = std::stof(v);
            else if (k == "partidas") p.gamesPlayed = std::stoi(v);
            else if (k == "pontos") p.totalPoints = std::stoi(v);
            else if (k == "pulos") p.totalJumps = std::stoi(v);
            else if (k == "fotos") p.photosTaken = std::stoi(v);
            else if (k == "duplas") p.duoGames = std::stoi(v);
            else if (k == "emblema") { if (!p.hasEmblem(v)) p.emblemsEarned.push_back(v); }
            else if (k.rfind("aventura.", 0) == 0) {
                const size_t i = std::stoul(k.substr(9));
                const auto comma = v.find(',');
                if (i < p.bestMedal.size() && comma != std::string::npos) {
                    p.bestMedal[i] = std::clamp(std::stoi(v.substr(0, comma)), 0, 4);
                    p.bestScore[i] = std::max(0, std::stoi(v.substr(comma + 1)));
                }
            } else if (k.rfind("livre.", 0) == 0) {
                int g = -1, d = -1;
                if (std::sscanf(k.c_str(), "livre.%d.%d", &g, &d) == 2 && g >= 0 && g < kGameCount && d >= 0 && d < 3)
                    p.freeBest[g][d] = std::max(0, std::stoi(v));
            }
        } catch (...) {
            // linha corrompida: ignora e continua
        }
    }
    *this = p;
    return true;
}

} // namespace k3d::game
