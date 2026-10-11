// Arquivos > 2 GB também em armeabi-v7a (API 24+).
#define _FILE_OFFSET_BITS 64
#include "recording.h"
#include "portable_io.h"

#include <sys/types.h>

#include <cstring>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace k3d {

namespace {
template <typename T> bool w(FILE *f, const T &v) { return std::fwrite(&v, sizeof(T), 1, f) == 1; }
template <typename T> bool r(FILE *f, T &v) { return std::fread(&v, sizeof(T), 1, f) == 1; }

bool writeHeader(FILE *f, const RecordHeader &h) {
    const uint8_t pad[3] = {0, 0, 0};
    const auto type = static_cast<uint8_t>(h.type);
    return w(f, kFrameMarker) && w(f, type) && std::fwrite(pad, 1, 3, f) == 3 && w(f, h.width) &&
           w(f, h.height) && w(f, h.deviceTs) && w(f, h.hostNs) && w(f, h.bytes);
}
} // namespace

bool Recorder::start(const std::string &path, const std::string &json, const std::vector<uint8_t> &calib,
                     std::string &err) {
    stop();
    f_ = openFileUtf8(path, "wb");
    if (!f_) { err = "Não foi possível criar " + path; return false; }
    std::setvbuf(f_, nullptr, _IOFBF, 4 << 20);
    const auto jl = static_cast<uint32_t>(json.size());
    const auto cl = static_cast<uint32_t>(calib.size());
    bool ok = std::fwrite(kRecMagic, 1, 8, f_) == 8 && w(f_, jl) &&
              std::fwrite(json.data(), 1, jl, f_) == jl && w(f_, cl) &&
              (cl == 0 || std::fwrite(calib.data(), 1, cl, f_) == cl);
    if (!ok) {
        std::fclose(f_); f_ = nullptr;
        err = "Falha ao escrever o cabeçalho da gravação";
        return false;
    }
    written_ = dropped_ = 0;
    bytes_ = 12 + jl + cl;
    ioError_ = false;
    stopping_ = false;
    active_ = true;
    th_ = std::thread(&Recorder::writerLoop, this);
    return true;
}

void Recorder::push(RecordType t, const void *data, uint32_t bytes, uint16_t width, uint16_t height,
                    uint32_t ts, int64_t hostNs) {
    if (!active_) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stopping_) return;
        if (queuedBytes_ + bytes > maxQueued_) { ++dropped_; return; }  // armazenamento lento
        Item it{{t, width, height, ts, hostNs, bytes}, {}};
        it.data.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + bytes);
        queuedBytes_ += bytes;
        q_.push_back(std::move(it));
    }
    cv_.notify_one();
}

void Recorder::writerLoop() {
    for (;;) {
        Item it;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return stopping_ || !q_.empty(); });
            if (q_.empty()) break;  // stopping_ e fila vazia
            it = std::move(q_.front());
            q_.pop_front();
            queuedBytes_ -= it.data.size();
        }
        if (ioError_) { ++dropped_; continue; }
        if (!writeHeader(f_, it.h) || std::fwrite(it.data.data(), 1, it.data.size(), f_) != it.data.size()) {
            ioError_ = true;  // provavelmente sem espaço
            ++dropped_;
            continue;
        }
        ++written_;
        bytes_ += 28 + it.data.size();
    }
}

void Recorder::stop() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!active_) return;
        stopping_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    if (f_) {
        std::fflush(f_);
#ifdef _WIN32
        _commit(_fileno(f_));
#else
        fsync(fileno(f_));
#endif
        std::fclose(f_);
        f_ = nullptr;
    }
    active_ = false;
}

Recorder::Stats Recorder::stats() const {
    return {written_.load(), dropped_.load(), bytes_.load(), active_.load(), ioError_.load()};
}

bool RecordingReader::open(const std::string &path, std::string &err) {
    f_ = openFileUtf8(path, "rb");
    if (!f_) { err = "Arquivo não encontrado"; return false; }
    char magic[8];
    if (std::fread(magic, 1, 8, f_) != 8 || std::memcmp(magic, kRecMagic, 8) != 0) {
        err = "Não é uma gravação do Kinect 3D Studio (.k3drec)";
        return false;
    }
    uint32_t jl = 0, cl = 0;
    if (!r(f_, jl) || jl > (16u << 20)) { err = "Cabeçalho inválido"; return false; }
    json_.resize(jl);
    if (jl && std::fread(&json_[0], 1, jl, f_) != jl) { err = "Cabeçalho incompleto"; return false; }
    if (!r(f_, cl) || cl > (64u << 20)) { err = "Calibração inválida"; return false; }
    calib_.resize(cl);
    if (cl && std::fread(calib_.data(), 1, cl, f_) != cl) { err = "Calibração incompleta"; return false; }

    // Indexa os registros; um registro incompleto no fim indica gravação interrompida.
    const off_t dataStart = ftello(f_);
    fseeko(f_, 0, SEEK_END);
    const off_t fileEnd = ftello(f_);
    fseeko(f_, dataStart, SEEK_SET);
    for (;;) {
        uint32_t marker; uint8_t type; uint8_t pad[3];
        RecordHeader h{};
        if (!r(f_, marker)) break;
        if (marker != kFrameMarker || !r(f_, type) || std::fread(pad, 1, 3, f_) != 3 || !r(f_, h.width) ||
            !r(f_, h.height) || !r(f_, h.deviceTs) || !r(f_, h.hostNs) || !r(f_, h.bytes)) {
            truncated_ = true;
            break;
        }
        h.type = static_cast<RecordType>(type);
        const off_t dataPos = ftello(f_);
        if (dataPos + static_cast<off_t>(h.bytes) > fileEnd) { truncated_ = true; break; }
        fseeko(f_, static_cast<off_t>(h.bytes), SEEK_CUR);
        index_.push_back({h, static_cast<uint64_t>(dataPos)});
    }
    if (index_.empty()) { err = "A gravação não contém quadros completos"; return false; }
    return true;
}

bool RecordingReader::read(const RecordingIndexEntry &e, std::vector<uint8_t> &out) {
    out.resize(e.h.bytes);
    if (fseeko(f_, static_cast<off_t>(e.offset), SEEK_SET) != 0) return false;
    return std::fread(out.data(), 1, e.h.bytes, f_) == e.h.bytes;
}

} // namespace k3d
