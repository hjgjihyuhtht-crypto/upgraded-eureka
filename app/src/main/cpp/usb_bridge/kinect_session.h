#pragma once
#include <atomic>
#include <string>
#include <thread>

#include "frame_source.h"

extern "C" {
#include "libfreenect.h"
#include "freenect_android_open.h"
}
struct libusb_context;

namespace k3d {

enum class VideoMode : int { None = 0, Rgb = 1, Ir = 2 };

// Captura ao vivo do Kinect v1 via libfreenect.
class KinectSession : public FrameSource {
public:
    KinectSession();
    ~KinectSession() override;

    bool isPlayback() const override { return false; }
    KinectSession *asLive() override { return this; }

    bool init(std::string &err);
    bool open(int cameraFd, int motorFd, std::string &err);
    bool startStreams(bool depth, VideoMode video, std::string &err);
    void stopStreams();
    void close();

    const freenect_android_info &info() const { return info_; }
    bool depthOn() const { return depthOn_; }
    VideoMode videoOn() const { return videoOn_; }

    bool hasMotor() const { return info_.motor_open != 0; }
    int setLed(int mode);
    int setTilt(double degrees);
    bool readMotor(double *out5);  // tilt, ax, ay, az, status

    static std::string drainLog();
    static void appendLog(const std::string &line);

private:
    static void depthCb(freenect_device *dev, void *data, uint32_t ts);
    static void videoCb(freenect_device *dev, void *data, uint32_t ts);
    void eventLoop();
    void stopEventThread();

    libusb_context *usb_ = nullptr;
    freenect_context *ctx_ = nullptr;
    freenect_device *dev_ = nullptr;
    freenect_android_info info_{};

    std::thread events_;
    std::atomic<bool> running_{false};
    bool depthOn_ = false;
    VideoMode videoOn_ = VideoMode::None;
};

} // namespace k3d
