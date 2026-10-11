#include "kinect/device_watch.h"

#include <chrono>
#include <libusb.h>

namespace k3d {

namespace {
constexpr int VID_MS = 0x045E;
constexpr int PID_NUI_CAMERA = 0x02AE, PID_NUI_MOTOR = 0x02B0, PID_NUI_AUDIO = 0x02AD;
constexpr int PID_K4W_CAMERA = 0x02BF, PID_K4W_AUDIO = 0x02BE, PID_K4W_AUDIO2 = 0x02C3, PID_K4W_AUDIO3 = 0x02BB;
constexpr int PID_V2_A = 0x02C4, PID_V2_B = 0x02D8, PID_V2_C = 0x02D9;
bool isAzure(int pid) { return pid >= 0x097A && pid <= 0x097E; }
} // namespace

std::string KinectUsbStatus::modelName() const {
    switch (state) {
        case State::V1Ready:
            if (cameraPid == PID_K4W_CAMERA) return "Kinect for Windows v1";
            return model1414 ? "Kinect v1 Xbox 360 (modelo 1414)" : "Kinect v1 Xbox 360 (modelo 1473)";
        case State::V1NoPower: return "Kinect v1 (sem energia na câmera)";
        case State::V2: return "Kinect v2 (Xbox One)";
        case State::Azure: return "Azure Kinect DK";
        default: return "";
    }
}

KinectUsbStatus classifyKinect(const std::vector<UsbDeviceInfo> &devs) {
    KinectUsbStatus s;
    bool v2 = false, azure = false, v1any = false;
    for (const auto &d : devs) {
        if (d.vid != VID_MS) continue;
        switch (d.pid) {
            case PID_NUI_CAMERA:
            case PID_K4W_CAMERA:
                s.state = KinectUsbStatus::State::V1Ready;
                s.cameraPid = d.pid;
                s.cameraBcd = d.bcd;
                s.model1414 = d.pid == PID_NUI_CAMERA && d.bcd == 0x010B;
                v1any = true;
                break;
            case PID_NUI_MOTOR: s.motorPresent = true; v1any = true; break;
            case PID_NUI_AUDIO: case PID_K4W_AUDIO: case PID_K4W_AUDIO2: case PID_K4W_AUDIO3:
                s.audioPresent = true; v1any = true; break;
            case PID_V2_A: case PID_V2_B: case PID_V2_C: v2 = true; break;
            default: if (isAzure(d.pid)) azure = true;
        }
    }
    if (s.state == KinectUsbStatus::State::V1Ready) {
        s.detail = "Câmera de profundidade, RGB e infravermelho disponível.";
    } else if (v1any) {
        s.state = KinectUsbStatus::State::V1NoPower;
        s.detail = "Só o motor/áudio do Kinect apareceu no USB. A câmera precisa da fonte de 12 V ligada na tomada.";
    } else if (v2) {
        s.state = KinectUsbStatus::State::V2;
        s.detail = "O Kinect v2 usa outro protocolo (USB 3.0, libfreenect2) e não é suportado por este programa.";
    } else if (azure) {
        s.state = KinectUsbStatus::State::Azure;
        s.detail = "O Azure Kinect depende do 'depth engine' da Microsoft e não é suportado por este programa.";
    }
    return s;
}

void DeviceWatcher::start(int intervalMs) {
    if (running_) return;
    intervalMs_ = intervalMs;
    running_ = true;
    th_ = std::thread(&DeviceWatcher::loop, this);
}

void DeviceWatcher::stop() {
    running_ = false;
    if (th_.joinable()) th_.join();
    if (ctx_) { libusb_exit(ctx_); ctx_ = nullptr; }
}

KinectUsbStatus DeviceWatcher::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

void DeviceWatcher::pollNow() { pollRequested_ = true; }

void DeviceWatcher::loop() {
    auto lastPoll = std::chrono::steady_clock::now() - std::chrono::hours(1);
    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        if (pollRequested_.exchange(false) ||
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPoll).count() >= intervalMs_) {
            lastPoll = now;
            if (!ctx_ && libusb_init(&ctx_) < 0) {
                // tenta de novo no próximo ciclo (ex.: sistema ainda iniciando o USB)
                ctx_ = nullptr;
                std::lock_guard<std::mutex> lk(mu_);
                status_.state = KinectUsbStatus::State::UsbError;
                status_.detail = "Não foi possível iniciar a biblioteca USB. Tentando de novo...";
                continue;
            }
            std::vector<UsbDeviceInfo> devs;
            libusb_device **list = nullptr;
            const ssize_t n = libusb_get_device_list(ctx_, &list);
            if (n >= 0) {
                for (ssize_t i = 0; i < n; ++i) {
                    libusb_device_descriptor d{};
                    if (libusb_get_device_descriptor(list[i], &d) < 0) continue;
                    devs.push_back({d.idVendor, d.idProduct, d.bcdDevice, libusb_get_bus_number(list[i]),
                                    libusb_get_device_address(list[i])});
                }
                libusb_free_device_list(list, 1);
            }
            KinectUsbStatus st = n >= 0 ? classifyKinect(devs) : KinectUsbStatus{};
            if (n < 0) {
                st.state = KinectUsbStatus::State::UsbError;
                st.detail = std::string("Erro ao listar o USB: ") + libusb_error_name(static_cast<int>(n));
            }
            bool changed = devs.size() != last_.size();
            for (size_t i = 0; !changed && i < devs.size(); ++i)
                changed = devs[i].pid != last_[i].pid || devs[i].vid != last_[i].vid || devs[i].address != last_[i].address;
            last_ = devs;
            std::lock_guard<std::mutex> lk(mu_);
            st.changeCount = status_.changeCount + (changed ? 1 : 0);
            status_ = st;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace k3d
