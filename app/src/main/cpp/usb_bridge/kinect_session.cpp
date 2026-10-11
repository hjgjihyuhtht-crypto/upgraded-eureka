#include "kinect_session.h"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <deque>
#include <libusb.h>

#ifdef __ANDROID__
#include <android/log.h>
#define K3D_LOG(...) __android_log_print(ANDROID_LOG_INFO, "K3D-native", __VA_ARGS__)
#else
#include <cstdio>
#define K3D_LOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#endif

namespace k3d {

namespace {
std::mutex gLogMutex;
std::deque<std::string> gLog;
constexpr size_t kMaxLog = 400;

void freenectLog(freenect_context *, freenect_loglevel level, const char *msg) {
    std::string line(msg ? msg : "");
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    const char *tag = level <= FREENECT_LOG_ERROR ? "ERRO" :
                      level <= FREENECT_LOG_WARNING ? "AVISO" : "info";
    KinectSession::appendLog(std::string("[libfreenect ") + tag + "] " + line);
}

} // namespace

void KinectSession::appendLog(const std::string &line) {
    K3D_LOG("%s", line.c_str());
    std::lock_guard<std::mutex> lk(gLogMutex);
    gLog.push_back(line);
    while (gLog.size() > kMaxLog) gLog.pop_front();
}

std::string KinectSession::drainLog() {
    std::lock_guard<std::mutex> lk(gLogMutex);
    std::string out;
    for (auto &l : gLog) { out += l; out += '\n'; }
    gLog.clear();
    return out;
}

KinectSession::KinectSession() = default;
KinectSession::~KinectSession() { close(); }

bool KinectSession::init(std::string &err) {
    // Obrigatório no Android: o app não tem acesso a /dev/bus/usb para enumerar.
    int r = libusb_set_option(nullptr, LIBUSB_OPTION_NO_DEVICE_DISCOVERY);
    if (r != LIBUSB_SUCCESS && r != LIBUSB_ERROR_NOT_SUPPORTED) {
        err = std::string("libusb_set_option falhou: ") + libusb_error_name(r);
        return false;
    }
    r = libusb_init(&usb_);
    if (r < 0) {
        err = std::string("libusb_init falhou: ") + libusb_error_name(r);
        return false;
    }
    if (freenect_init(&ctx_, reinterpret_cast<freenect_usb_context *>(usb_)) < 0) {
        err = "freenect_init falhou";
        libusb_exit(usb_);
        usb_ = nullptr;
        return false;
    }
    freenect_set_log_level(ctx_, FREENECT_LOG_NOTICE);
    freenect_set_log_callback(ctx_, freenectLog);
    appendLog("Contexto libusb + libfreenect criado (sem enumeração de barramento)");
    return true;
}

bool KinectSession::open(int cameraFd, int motorFd, std::string &err) {
    if (!ctx_) { err = "Sessão não inicializada"; return false; }
    if (dev_) { err = "Dispositivo já aberto"; return false; }
    char buf[256] = {0};
    int r = freenect_android_open(ctx_, &dev_, cameraFd, motorFd, &info_, buf, sizeof(buf));
    if (r < 0) {
        err = buf[0] ? buf : "Falha ao abrir o Kinect";
        dev_ = nullptr;
        return false;
    }
    freenect_set_user(dev_, this);
    calib_ = Calibration::fromDevice(dev_);
    appendLog(calib_.valid ? "Calibração de fábrica lida: f=" + std::to_string(calib_.fx()) + " px"
                           : "AVISO: calibração indisponível; distâncias em mm não serão calculadas");
    char line[200];
    snprintf(line, sizeof(line), "Kinect aberto: PID=0x%04x bcdDevice=0x%04x modelo=%s motor=%s",
             info_.camera_pid, info_.camera_bcd,
             info_.is_model_1414 ? "1414" : "1473/K4W",
             info_.motor_open ? "sim" : "não");
    appendLog(line);
    return true;
}

bool KinectSession::startStreams(bool depth, VideoMode video, std::string &err) {
    if (!dev_) { err = "Kinect não está aberto"; return false; }
    stopStreams();

    resetStats();
    if (!depth) store_.clearDepth();
    if (video == VideoMode::None) store_.clearVideo();

    if (depth) {
        // Profundidade bruta de 11 bits (dado original do sensor); mm via calibração.
        freenect_frame_mode m = freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_11BIT);
        if (!m.is_valid || freenect_set_depth_mode(dev_, m) < 0) {
            err = "Modo de profundidade 640x480 (11 bits) não aceito pelo sensor";
            return false;
        }
        freenect_set_depth_callback(dev_, &KinectSession::depthCb);
        if (freenect_start_depth(dev_) < 0) { err = "freenect_start_depth falhou"; return false; }
        depthOn_ = true;
    }
    if (video != VideoMode::None) {
        freenect_video_format fmt = video == VideoMode::Rgb ? FREENECT_VIDEO_RGB : FREENECT_VIDEO_IR_8BIT;
        freenect_frame_mode m = freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, fmt);
        if (!m.is_valid || freenect_set_video_mode(dev_, m) < 0) {
            err = "Modo de vídeo não aceito pelo sensor";
            stopStreams();
            return false;
        }
        freenect_set_video_callback(dev_, &KinectSession::videoCb);
        if (freenect_start_video(dev_) < 0) {
            err = "freenect_start_video falhou";
            stopStreams();
            return false;
        }
        videoOn_ = video;
    }

    if (!running_) {
        running_ = true;
        events_ = std::thread(&KinectSession::eventLoop, this);
    }
    appendLog(std::string("Fluxos iniciados: profundidade=") + (depth ? "sim" : "não") +
              " vídeo=" + (video == VideoMode::Rgb ? "RGB" : video == VideoMode::Ir ? "IR" : "não"));
    return true;
}

void KinectSession::eventLoop() {
    timeval tv{0, 100 * 1000};
    while (running_) {
        int r = freenect_process_events_timeout(ctx_, &tv);
        if (r < 0) {
            if (dev_ && freenect_android_camera_dead(dev_)) {
                dead_ = true;
                appendLog("Kinect desconectado durante a transmissão");
                running_ = false;
                break;
            }
            if (r != LIBUSB_ERROR_INTERRUPTED) {
                appendLog(std::string("Erro no laço de eventos USB: ") + libusb_error_name(r));
            }
        }
    }
}

void KinectSession::stopEventThread() {
    running_ = false;
    if (events_.joinable()) events_.join();
}

void KinectSession::stopStreams() {
    if (!dev_) return;
    if (depthOn_) { freenect_stop_depth(dev_); depthOn_ = false; }
    if (videoOn_ != VideoMode::None) { freenect_stop_video(dev_); videoOn_ = VideoMode::None; }
    // Mantém a thread rodando por pouco tempo para drenar transferências canceladas.
    stopEventThread();
}

void KinectSession::close() {
    stopRecording();
    stopStreams();
    if (dev_) {
        if (hasMotor()) freenect_set_led(dev_, LED_BLINK_GREEN);
        freenect_close_device(dev_);
        dev_ = nullptr;
        appendLog("Kinect fechado");
    }
    info_ = {};
    if (ctx_) { freenect_shutdown(ctx_); ctx_ = nullptr; }
    if (usb_) { libusb_exit(usb_); usb_ = nullptr; }
}

void KinectSession::depthCb(freenect_device *dev, void *data, uint32_t ts) {
    auto *self = static_cast<KinectSession *>(freenect_get_user(dev));
    if (!self || !data) return;
    const freenect_frame_mode m = freenect_get_current_depth_mode(dev);
    self->onDepth(static_cast<const uint16_t *>(data), m.width, m.height, ts);
}

void KinectSession::videoCb(freenect_device *dev, void *data, uint32_t ts) {
    auto *self = static_cast<KinectSession *>(freenect_get_user(dev));
    if (!self || !data) return;
    const freenect_frame_mode m = freenect_get_current_video_mode(dev);
    const VideoFormat f = m.video_format == FREENECT_VIDEO_RGB ? VideoFormat::Rgb : VideoFormat::Ir8;
    self->onVideo(static_cast<const uint8_t *>(data), static_cast<size_t>(m.bytes), f, m.width, m.height, ts);
}

int KinectSession::setLed(int mode) {
    if (!dev_ || !hasMotor()) return -1;
    return freenect_set_led(dev_, static_cast<freenect_led_options>(mode));
}

int KinectSession::setTilt(double degrees) {
    if (!dev_ || !hasMotor()) return -1;
    degrees = std::max(-27.0, std::min(27.0, degrees));  // limite mecânico documentado
    return freenect_set_tilt_degs(dev_, degrees);
}

bool KinectSession::readMotor(double *o) {
    if (!dev_ || !hasMotor()) return false;
    if (freenect_update_tilt_state(dev_) < 0) return false;
    freenect_raw_tilt_state *st = freenect_get_tilt_state(dev_);
    if (!st) return false;
    o[0] = freenect_get_tilt_degs(st);
    freenect_get_mks_accel(st, &o[1], &o[2], &o[3]);
    o[4] = static_cast<double>(freenect_get_tilt_status(st));
    return true;
}

} // namespace k3d
