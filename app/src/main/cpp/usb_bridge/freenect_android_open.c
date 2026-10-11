/*
 * Kinect 3D Studio — abertura do Kinect v1 a partir de file descriptors do Android.
 *
 * No Android, um app sem root não pode enumerar /dev/bus/usb. O caminho correto é:
 *   1. Kotlin: UsbManager.requestPermission() + UsbManager.openDevice() -> fd.
 *   2. Nativo: libusb_wrap_sys_device(fd) -> libusb_device_handle.
 *
 * O libfreenect original só abre dispositivos enumerando o barramento
 * (fnusb_open_subdevices). Este arquivo reproduz a mesma lógica, mas
 * recebendo os handles já embrulhados. É compilado dentro da mesma .so que
 * o libfreenect, por isso tem acesso aos símbolos FN_INTERNAL.
 *
 * Licença: Apache-2.0 (mesma opção de licença do libfreenect).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "freenect_internal.h"
#include "cameras.h"
#include "freenect_android_open.h"

/* Declarado em usb_libusb10.c, sem cabeçalho público. */
int fnusb_claim_camera(freenect_device *dev);

static void set_err(char *err, size_t len, const char *msg, int code)
{
	if (!err || len == 0) return;
	if (code != 0)
		snprintf(err, len, "%s (%d: %s)", msg, code, libusb_error_name(code));
	else
		snprintf(err, len, "%s", msg);
}

static int wrap_fd(freenect_context *ctx, int fd, libusb_device_handle **out,
                   struct libusb_device_descriptor *desc)
{
	int r = libusb_wrap_sys_device(ctx->usb.ctx, (intptr_t)fd, out);
	if (r < 0 || !*out) return r < 0 ? r : LIBUSB_ERROR_OTHER;
	r = libusb_get_device_descriptor(libusb_get_device(*out), desc);
	if (r < 0) {
		libusb_close(*out);
		*out = NULL;
	}
	return r;
}

int freenect_android_open(freenect_context *ctx, freenect_device **out,
                          int camera_fd, int motor_fd,
                          freenect_android_info *info, char *err, size_t err_len)
{
	int r;
	struct libusb_device_descriptor cam_desc, mot_desc;
	freenect_device *pdev;

	*out = NULL;
	if (info) memset(info, 0, sizeof(*info));
	if (camera_fd < 0) {
		set_err(err, err_len, "Câmera do Kinect não foi aberta pelo Android (fd inválido)", 0);
		return -1;
	}

	pdev = (freenect_device *)calloc(1, sizeof(freenect_device));
	if (!pdev) {
		set_err(err, err_len, "Sem memória", 0);
		return -1;
	}
	pdev->parent = ctx;
	pdev->usb_cam.parent = pdev;
	pdev->usb_motor.parent = pdev;
	pdev->usb_audio.parent = pdev;

	/* ---------- Câmera (RGB, IR e profundidade) ---------- */
	r = wrap_fd(ctx, camera_fd, &pdev->usb_cam.dev, &cam_desc);
	if (r < 0) {
		set_err(err, err_len, "libusb não conseguiu usar o fd da câmera", r);
		free(pdev);
		return r;
	}
	if (cam_desc.idVendor != VID_MICROSOFT ||
	    (cam_desc.idProduct != PID_NUI_CAMERA && cam_desc.idProduct != PID_K4W_CAMERA)) {
		set_err(err, err_len, "O fd recebido não é a câmera de um Kinect v1", 0);
		libusb_close(pdev->usb_cam.dev);
		free(pdev);
		return -1;
	}

	pdev->usb_cam.VID = cam_desc.idVendor;
	pdev->usb_cam.PID = cam_desc.idProduct;

	/* Mesma regra do libfreenect: 1414 tem bcdDevice 267 (0x010B). */
	int is_1414 = (cam_desc.idProduct == PID_NUI_CAMERA && cam_desc.bcdDevice == 267);
	ctx->zero_plane_res = is_1414 ? 322 : 334;
	pdev->device_does_motor_control_with_audio = is_1414 ? 0 : 1;
	pdev->motor_control_with_audio_enabled = 0;

	if (info) {
		info->camera_pid = cam_desc.idProduct;
		info->camera_bcd = cam_desc.bcdDevice;
		info->is_model_1414 = is_1414;
	}

	r = fnusb_claim_camera(pdev); /* em caso de erro, já fecha o handle */
	if (r < 0) {
		set_err(err, err_len, "Não foi possível reivindicar a interface 0 da câmera", r);
		free(pdev);
		return r;
	}

	/* ---------- Motor (inclinação, LED, acelerômetro) — só no 1414 ---------- */
	freenect_device_flags flags = FREENECT_DEVICE_CAMERA;
	if (motor_fd >= 0 && is_1414) {
		r = wrap_fd(ctx, motor_fd, &pdev->usb_motor.dev, &mot_desc);
		if (r >= 0 && mot_desc.idProduct == PID_NUI_MOTOR) {
			r = libusb_claim_interface(pdev->usb_motor.dev, 0);
			if (r >= 0) {
				pdev->usb_motor.VID = mot_desc.idVendor;
				pdev->usb_motor.PID = mot_desc.idProduct;
				flags = (freenect_device_flags)(flags | FREENECT_DEVICE_MOTOR);
				if (info) info->motor_open = 1;
			} else {
				libusb_close(pdev->usb_motor.dev);
				pdev->usb_motor.dev = NULL;
			}
		} else if (pdev->usb_motor.dev) {
			libusb_close(pdev->usb_motor.dev);
			pdev->usb_motor.dev = NULL;
		}
	}
	/* Nos modelos 1473 / Kinect for Windows o motor é controlado pelo
	 * dispositivo de áudio, que exige upload do firmware "audios.bin"
	 * (proprietário da Microsoft, não redistribuível). Nesta etapa o motor
	 * fica indisponível nesses modelos; câmera, IR e profundidade funcionam. */
	ctx->enabled_subdevices = flags;

	/* Encadeia no contexto, como freenect_open_device faz. */
	if (!ctx->first) {
		ctx->first = pdev;
	} else {
		freenect_device *prev = ctx->first;
		while (prev->next) prev = prev->next;
		prev->next = pdev;
	}

	r = freenect_camera_init(pdev);
	if (r < 0) {
		set_err(err, err_len, "Falha ao inicializar a câmera (leitura de registros/calibração)", 0);
		freenect_close_device(pdev);
		return r;
	}

	*out = pdev;
	return 0;
}

int freenect_android_camera_dead(freenect_device *dev)
{
	return dev ? dev->usb_cam.device_dead : 0;
}
