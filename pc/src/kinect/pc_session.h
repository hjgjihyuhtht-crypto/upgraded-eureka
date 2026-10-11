#pragma once
// Sessão do Kinect v1 no PC (libfreenect abrindo o dispositivo pelo USB).
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "frame_source.h"

struct _freenect_context;
struct _freenect_device;

namespace k3d {

enum class PcVideo : int { None = 0, Rgb = 1, Ir = 2 };

class PcKinectSession : public FrameSource {
public:
    ~PcKinectSession() override;
    bool isPlayback() const override { return false; }

    // wantMotor: só no modelo 1414 (nos outros o motor exige firmware proprietário).
    bool open(bool wantMotor, std::string &err);
    bool startStreams(bool depth, PcVideo video, std::string &err);
    void stopStreams();
    void close();
    bool isOpen() const { return dev_ != nullptr; }
    bool deviceLost() const { return dead_.load(); }
    PcVideo video() const { return video_; }

    bool hasMotor() const { return motor_; }
    bool setLed(int mode);
    bool setTilt(double degrees);
    bool readTilt(double &degrees, double &ax, double &ay, double &az);

    static std::string drainLog();
    static void log(const std::string &line);

private:
    static void depthCb(_freenect_device *dev, void *data, uint32_t ts);
    static void videoCb(_freenect_device *dev, void *data, uint32_t ts);
    void loop();

    _freenect_context *ctx_ = nullptr;
    _freenect_device *dev_ = nullptr;
    std::thread th_;
    std::atomic<bool> running_{false};
    bool depthOn_ = false, motor_ = false;
    PcVideo video_ = PcVideo::None;
    std::mutex devMu_;   // serializa comandos ao dispositivo
};

// Dica de solução para erros comuns de abertura (driver, permissão, energia).
std::string kinectOpenHint(const std::string &log);

} // namespace k3d
