#pragma once
// Formato de gravação .k3drec (little-endian), documentado em docs/FORMATOS.md:
//
//   cabeçalho: "K3DREC" 0x00 0x01 | u32 jsonLen | json | u32 calibLen | calibração
//   registros: u32 0x314D5246 ("FRM1") | u8 tipo | u8[3] 0 | u16 w | u16 h
//              | u32 timestampSensor | i64 hostNs | u32 bytes | dados
//
// tipo 1 = profundidade bruta (uint16 por pixel), 2 = RGB888, 3 = IR 8 bits.
// Um arquivo cortado (bateria acabou, app fechado) é lido até o último registro completo.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace k3d {

enum class RecordType : uint8_t { Depth = 1, Rgb = 2, Ir8 = 3 };

constexpr uint32_t kFrameMarker = 0x314D5246;  // "FRM1"
constexpr char kRecMagic[8] = {'K', '3', 'D', 'R', 'E', 'C', 0, 1};

struct RecordHeader {
    RecordType type;
    uint16_t width, height;
    uint32_t deviceTs;
    int64_t hostNs;
    uint32_t bytes;
};

class Recorder {
public:
    struct Stats { uint64_t framesWritten, framesDropped, bytesWritten; bool active; bool ioError; };

    // maxQueuedBytes limita a RAM usada pela fila (quadros extras são descartados e contados).
    explicit Recorder(size_t maxQueuedBytes = 48u * 1024u * 1024u) : maxQueued_(maxQueuedBytes) {}
    ~Recorder() { stop(); }

    bool start(const std::string &path, const std::string &json, const std::vector<uint8_t> &calib, std::string &err);
    void push(RecordType t, const void *data, uint32_t bytes, uint16_t w, uint16_t h, uint32_t ts, int64_t hostNs);
    void stop();
    Stats stats() const;
    bool active() const { return active_.load(); }

private:
    struct Item { RecordHeader h; std::vector<uint8_t> data; };
    void writerLoop();

    FILE *f_ = nullptr;
    std::thread th_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Item> q_;
    size_t queuedBytes_ = 0;
    const size_t maxQueued_;
    std::atomic<bool> active_{false}, ioError_{false};
    bool stopping_ = false;
    std::atomic<uint64_t> written_{0}, dropped_{0}, bytes_{0};
};

struct RecordingIndexEntry { RecordHeader h; uint64_t offset; };

// Leitura de arquivos .k3drec (índice dos registros sem carregar os dados).
class RecordingReader {
public:
    ~RecordingReader() { if (f_) std::fclose(f_); }
    bool open(const std::string &path, std::string &err);
    const std::string &json() const { return json_; }
    const std::vector<uint8_t> &calibration() const { return calib_; }
    const std::vector<RecordingIndexEntry> &index() const { return index_; }
    bool truncated() const { return truncated_; }
    bool read(const RecordingIndexEntry &e, std::vector<uint8_t> &out);

private:
    FILE *f_ = nullptr;
    std::string json_;
    std::vector<uint8_t> calib_;
    std::vector<RecordingIndexEntry> index_;
    bool truncated_ = false;
};

} // namespace k3d
