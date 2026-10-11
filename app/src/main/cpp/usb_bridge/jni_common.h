#pragma once
#include <jni.h>
#include <string>
#include "frame_source.h"

#define JNI_FN(name) Java_com_kinect3dstudio_nativebridge_NativeKinect_##name

namespace k3d {
extern thread_local std::string tLastError;
inline FrameSource *fromHandle(jlong h) { return reinterpret_cast<FrameSource *>(h); }
inline jlong toHandle(FrameSource *s) { return reinterpret_cast<jlong>(s); }
} // namespace k3d
