#pragma once
#include <stddef.h>
#include "libfreenect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int camera_pid;
	int camera_bcd;
	int is_model_1414;
	int motor_open;
} freenect_android_info;

/*
 * Abre um Kinect v1 usando file descriptors obtidos por UsbDeviceConnection.
 * O contexto precisa ter sido criado com LIBUSB_OPTION_NO_DEVICE_DISCOVERY.
 * Os fds continuam pertencendo ao Kotlin: libusb_close não os fecha.
 * motor_fd pode ser -1.
 */
int freenect_android_open(freenect_context *ctx, freenect_device **out,
                          int camera_fd, int motor_fd,
                          freenect_android_info *info, char *err, size_t err_len);

/* 1 se o libusb marcou a câmera como removida (cabo/energia). */
int freenect_android_camera_dead(freenect_device *dev);

#ifdef __cplusplus
}
#endif
