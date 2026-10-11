#pragma once
// Vigia o USB (a cada segundo) para saber se há um Kinect conectado e em que estado.
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct libusb_context;

namespace k3d {

struct UsbDeviceInfo { int vid, pid, bcd, bus, address; };

struct KinectUsbStatus {
    enum class State {
        None,          // nenhum Kinect
        V1Ready,       // câmera do Kinect v1 presente (pode abrir)
        V1NoPower,     // só motor/áudio: falta a fonte de 12 V
        V2,            // Kinect v2: não suportado
        Azure,         // Azure Kinect: não suportado
        UsbError,      // não foi possível listar o USB
    };
    State state = State::None;
    int cameraPid = 0, cameraBcd = 0;
    bool motorPresent = false, audioPresent = false;
    bool model1414 = false;
    std::string detail;
    uint64_t changeCount = 0;   // muda a cada alteração do conjunto de dispositivos

    std::string modelName() const;
};

KinectUsbStatus classifyKinect(const std::vector<UsbDeviceInfo> &devices);

class DeviceWatcher {
public:
    DeviceWatcher() = default;
    ~DeviceWatcher() { stop(); }
    void start(int intervalMs = 1000);
    void stop();
    KinectUsbStatus status() const;
    void pollNow();

private:
    void loop();
    libusb_context *ctx_ = nullptr;
    std::thread th_;
    std::atomic<bool> running_{false}, pollRequested_{false};
    int intervalMs_ = 1000;
    mutable std::mutex mu_;
    KinectUsbStatus status_;
    std::vector<UsbDeviceInfo> last_;
};

} // namespace k3d
