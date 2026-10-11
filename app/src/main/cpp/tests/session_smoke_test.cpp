// Teste de fumaça do host: garante que a sessão inicializa sem enumerar o
// barramento e que um fd inválido produz erro legível (sem travar ou crashar).
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kinect_session.h"

int main() {
    // No Linux de desktop o libusb exige /dev/bus/usb com algum nó; no Android
    // ele assume o caminho mesmo sem conseguir listá-lo (SELinux).
    struct stat sb;
    if (stat("/dev/bus/usb", &sb) != 0) {
        std::printf("SKIP: host sem /dev/bus/usb (contêiner sem USB)\n");
        return 77;
    }
    std::string err;
    {
        k3d::KinectSession s;
        bool ok = s.init(err);
        if (!ok) { std::printf("init falhou: %s\n", err.c_str()); std::fflush(stdout); }
        assert(ok && "init deve funcionar sem dispositivos");
        assert(!s.open(-1, -1, err));
        std::printf("fd -1 -> %s\n", err.c_str());
    }
    {
        k3d::KinectSession s;
        assert(s.init(err));
        int fd = ::open("/dev/null", O_RDWR);
        assert(!s.open(fd, -1, err));
        std::printf("/dev/null -> %s\n", err.c_str());
        ::close(fd);
        double st[k3d::S_COUNT];
        s.readStats(st);
        assert(st[k3d::S_DEPTH_FRAMES] == 0);
        assert(!s.hasMotor() && s.setTilt(5) == -1);
        std::string err2;
        assert(!s.startStreams(true, k3d::VideoMode::Rgb, err2));
        std::printf("startStreams sem dispositivo -> %s\n", err2.c_str());
    }
    std::printf("log:\n%s", k3d::KinectSession::drainLog().c_str());
    std::printf("OK\n");
    return 0;
}
