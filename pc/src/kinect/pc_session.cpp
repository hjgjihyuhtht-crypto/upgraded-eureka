#include "kinect/pc_session.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

extern "C" {
#include "libfreenect.h"
}

namespace k3d {

namespace {
std::mutex gLogMu;
std::deque<std::string> gLog;

void fnLog(freenect_context *, freenect_loglevel level, const char *msg) {
    std::string s(msg ? msg : "");
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    const char *tag = level <= FREENECT_LOG_ERROR ? "ERRO" : level <= FREENECT_LOG_WARNING ? "AVISO" : "info";
    PcKinectSession::log(std::string("[libfreenect ") + tag + "] " + s);
}
} // namespace

void PcKinectSession::log(const std::string &line) {
    std::lock_guard<std::mutex> lk(gLogMu);
    gLog.push_back(line);
    while (gLog.size() > 300) gLog.pop_front();
}

std::string PcKinectSession::drainLog() {
    std::lock_guard<std::mutex> lk(gLogMu);
    std::string out;
    for (auto &l : gLog) { out += l; out += '\n'; }
    gLog.clear();
    return out;
}

std::string kinectOpenHint(const std::string &log) {
#ifdef _WIN32
    if (log.find("NOT_SUPPORTED") != std::string::npos || log.find("NOT_FOUND") != std::string::npos ||
        log.find("ACCESS") != std::string::npos)
        return "O Windows está usando outro driver para o Kinect. Instale o driver libusbK com o Zadig para "
               "\"Xbox NUI Camera\" e \"Xbox NUI Motor\" (veja LEIA-ME.txt) e reconecte o Kinect.";
    if (log.find("BUSY") != std::string::npos)
        return "Outro programa está usando o Kinect. Feche-o e tente de novo.";
#else
    if (log.find("ACCESS") != std::string::npos)
        return "Sem permissão para o USB do Kinect. Instale a regra udev (51-kinect.rules, veja o LEIA-ME) "
               "ou rode com sudo uma vez para testar.";
    if (log.find("BUSY") != std::string::npos)
        return "Outro programa (ou o driver gspca_kinect do sistema) está usando o Kinect.";
#endif
    if (log.find("Could not find") != std::string::npos)
        return "A câmera do Kinect não respondeu. Confira a fonte de 12 V e o cabo.";
    return "Confira a fonte de 12 V, o cabo USB e reconecte o Kinect.";
}

PcKinectSession::~PcKinectSession() { close(); }

bool PcKinectSession::open(bool wantMotor, std::string &err) {
    close();
    if (freenect_init(&ctx_, nullptr) < 0) { err = "Não foi possível iniciar o libfreenect"; ctx_ = nullptr; return false; }
    freenect_set_log_level(ctx_, FREENECT_LOG_NOTICE);
    freenect_set_log_callback(ctx_, fnLog);
    freenect_select_subdevices(ctx_, static_cast<freenect_device_flags>(
                                         FREENECT_DEVICE_CAMERA | (wantMotor ? FREENECT_DEVICE_MOTOR : 0)));
    if (freenect_num_devices(ctx_) < 1) {
        err = "Nenhuma câmera de Kinect encontrada pelo libfreenect";
        freenect_shutdown(ctx_); ctx_ = nullptr;
        return false;
    }
    int r = freenect_open_device(ctx_, &dev_, 0);
    if (r < 0 && wantMotor) {
        // tenta de novo só com a câmera
        log("Abertura com motor falhou; tentando só a câmera");
        freenect_select_subdevices(ctx_, FREENECT_DEVICE_CAMERA);
        r = freenect_open_device(ctx_, &dev_, 0);
        wantMotor = false;
    }
    if (r < 0) {
        err = "Não foi possível abrir o Kinect";
        dev_ = nullptr;
        freenect_shutdown(ctx_); ctx_ = nullptr;
        return false;
    }
    motor_ = wantMotor;
    freenect_set_user(dev_, this);
    calib_ = Calibration::fromDevice(dev_);
    log(calib_.valid ? "Calibração de fábrica lida (f = " + std::to_string(calib_.fx()) + " px)"
                     : "AVISO: calibração indisponível");
    dead_ = false;
    running_ = true;
    th_ = std::thread(&PcKinectSession::loop, this);
    if (motor_) setLed(LED_GREEN);
    return true;
}

void PcKinectSession::loop() {
    int errors = 0;
    while (running_) {
        timeval tv{0, 50 * 1000};
        const int r = freenect_process_events_timeout(ctx_, &tv);
        // erros isolados acontecem; vários seguidos indicam que o Kinect saiu do USB
        errors = r < 0 ? errors + 1 : 0;
        if (errors >= 5 && !dead_) {
            log("Erros seguidos no USB (" + std::to_string(r) + "): o Kinect pode ter sido desconectado");
            dead_ = true;
        }
        if (r < 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool PcKinectSession::startStreams(bool depth, PcVideo video, std::string &err) {
    std::lock_guard<std::mutex> lk(devMu_);
    if (!dev_) { err = "Kinect não está aberto"; return false; }
    if (depthOn_) { freenect_stop_depth(dev_); depthOn_ = false; }
    if (video_ != PcVideo::None) { freenect_stop_video(dev_); video_ = PcVideo::None; }
    resetStats();
    if (!depth) store_.clearDepth();
    if (video == PcVideo::None) store_.clearVideo();
    if (depth) {
        const freenect_frame_mode m = freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_11BIT);
        if (!m.is_valid || freenect_set_depth_mode(dev_, m) < 0) { err = "Modo de profundidade não aceito"; return false; }
        freenect_set_depth_callback(dev_, &PcKinectSession::depthCb);
        if (freenect_start_depth(dev_) < 0) { err = "Falha ao iniciar a profundidade"; return false; }
        depthOn_ = true;
    }
    if (video != PcVideo::None) {
        const freenect_frame_mode m = freenect_find_video_mode(
            FREENECT_RESOLUTION_MEDIUM, video == PcVideo::Rgb ? FREENECT_VIDEO_RGB : FREENECT_VIDEO_IR_8BIT);
        if (!m.is_valid || freenect_set_video_mode(dev_, m) < 0) { err = "Modo de vídeo não aceito"; return false; }
        freenect_set_video_callback(dev_, &PcKinectSession::videoCb);
        if (freenect_start_video(dev_) < 0) { err = "Falha ao iniciar o vídeo"; return false; }
        video_ = video;
    }
    return true;
}

void PcKinectSession::stopStreams() {
    std::lock_guard<std::mutex> lk(devMu_);
    if (!dev_) return;
    if (depthOn_) { freenect_stop_depth(dev_); depthOn_ = false; }
    if (video_ != PcVideo::None) { freenect_stop_video(dev_); video_ = PcVideo::None; }
}

void PcKinectSession::close() {
    stopRecording();
    if (dev_ && !dead_) {
        stopStreams();
        if (motor_) freenect_set_led(dev_, LED_BLINK_GREEN);
    }
    running_ = false;
    if (th_.joinable()) th_.join();
    if (dev_) { freenect_close_device(dev_); dev_ = nullptr; }
    if (ctx_) { freenect_shutdown(ctx_); ctx_ = nullptr; }
    depthOn_ = false;
    video_ = PcVideo::None;
    motor_ = false;
}

void PcKinectSession::depthCb(_freenect_device *dev, void *data, uint32_t ts) {
    auto *self = static_cast<PcKinectSession *>(freenect_get_user(dev));
    if (!self || !data) return;
    const freenect_frame_mode m = freenect_get_current_depth_mode(dev);
    self->onDepth(static_cast<const uint16_t *>(data), m.width, m.height, ts);
}

void PcKinectSession::videoCb(_freenect_device *dev, void *data, uint32_t ts) {
    auto *self = static_cast<PcKinectSession *>(freenect_get_user(dev));
    if (!self || !data) return;
    const freenect_frame_mode m = freenect_get_current_video_mode(dev);
    const VideoFormat f = m.video_format == FREENECT_VIDEO_RGB ? VideoFormat::Rgb : VideoFormat::Ir8;
    self->onVideo(static_cast<const uint8_t *>(data), static_cast<size_t>(m.bytes), f, m.width, m.height, ts);
}

bool PcKinectSession::setLed(int mode) {
    std::lock_guard<std::mutex> lk(devMu_);
    return dev_ && motor_ && freenect_set_led(dev_, static_cast<freenect_led_options>(mode)) >= 0;
}

bool PcKinectSession::setTilt(double deg) {
    std::lock_guard<std::mutex> lk(devMu_);
    return dev_ && motor_ && freenect_set_tilt_degs(dev_, std::clamp(deg, -27.0, 27.0)) >= 0;
}

bool PcKinectSession::readTilt(double &deg, double &ax, double &ay, double &az) {
    std::lock_guard<std::mutex> lk(devMu_);
    if (!dev_ || !motor_ || freenect_update_tilt_state(dev_) < 0) return false;
    freenect_raw_tilt_state *st = freenect_get_tilt_state(dev_);
    if (!st) return false;
    deg = freenect_get_tilt_degs(st);
    freenect_get_mks_accel(st, &ax, &ay, &az);
    return true;
}

} // namespace k3d
