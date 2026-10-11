#pragma once
// Declarações da API C do NiTE 2.2 (PrimeSense), usadas para carregar libNiTE2.so em tempo de execução.
//
// O NiTE2 é proprietário e foi descontinuado: este projeto NÃO inclui nem redistribui o NiTE.
// Estas declarações foram escritas para este projeto a partir da documentação pública da API C
// (NiteCAPI.h / NiteCTypes.h). O carregador confere versão e valores dos quadros antes de confiar neles.
#include <cstdint>

#include "OniCTypes.h"

extern "C" {

typedef short NiteUserId;
typedef struct NiteUserTracker *NiteUserTrackerHandle;

enum NiteStatusC { NITE_STATUS_OK = 0, NITE_STATUS_ERROR = 1, NITE_STATUS_BAD_USER_ID = 2, NITE_STATUS_OUT_OF_FLOW = 3 };
typedef int NiteStatus;

enum NiteJointTypeC {
    NITE_JOINT_HEAD = 0,
    NITE_JOINT_NECK,
    NITE_JOINT_LEFT_SHOULDER,
    NITE_JOINT_RIGHT_SHOULDER,
    NITE_JOINT_LEFT_ELBOW,
    NITE_JOINT_RIGHT_ELBOW,
    NITE_JOINT_LEFT_HAND,
    NITE_JOINT_RIGHT_HAND,
    NITE_JOINT_TORSO,
    NITE_JOINT_LEFT_HIP,
    NITE_JOINT_RIGHT_HIP,
    NITE_JOINT_LEFT_KNEE,
    NITE_JOINT_RIGHT_KNEE,
    NITE_JOINT_LEFT_FOOT,
    NITE_JOINT_RIGHT_FOOT,
};
enum { NITE_JOINT_COUNT = 15, NITE_POSE_COUNT = 2 };

enum NiteSkeletonStateC {
    NITE_SKELETON_NONE = 0,
    NITE_SKELETON_CALIBRATING = 1,
    NITE_SKELETON_TRACKED = 2,
    // 3..6: falhas de calibração
};
enum NiteUserStateC { NITE_USER_STATE_VISIBLE = 1, NITE_USER_STATE_NEW = 2, NITE_USER_STATE_LOST = 4 };

typedef struct { float x, y, z; } NitePoint3f;
typedef struct { float x, y, z, w; } NiteQuaternion;

typedef struct {
    int jointType;
    NitePoint3f position;          // mm, coordenadas do mundo do OpenNI (Y para cima)
    float positionConfidence;
    NiteQuaternion orientation;
    float orientationConfidence;
} NiteSkeletonJoint;

typedef struct { NitePoint3f min, max; } NiteBoundingBox;
typedef struct { int type; int state; } NitePoseData;

typedef struct {
    NiteSkeletonJoint joints[NITE_JOINT_COUNT];
    int state;
} NiteSkeleton;

typedef struct {
    NiteUserId id;
    NiteBoundingBox boundingBox;
    NitePoint3f centerOfMass;
    int state;
    NiteSkeleton skeleton;
    NitePoseData poses[NITE_POSE_COUNT];
} NiteUserData;

typedef struct {
    NiteUserId *pixels;
    int width, height;
    int stride;  // bytes por linha
} NiteUserMap;

typedef struct { NitePoint3f point, normal; } NitePlane;

typedef struct {
    int userCount;
    NiteUserData *pUser;
    NiteUserMap userMap;
    OniFrame *pDepthFrame;
    unsigned long long timestamp;
    int frameIndex;
    float floorConfidence;
    NitePlane floor;
} NiteUserTrackerFrame;

typedef struct { int major, minor, maintenance, build; } NiteVersion;

typedef NiteStatus (*PFN_niteInitialize)();
typedef void (*PFN_niteShutdown)();
typedef NiteVersion (*PFN_niteGetVersion)();
typedef NiteStatus (*PFN_niteInitializeUserTracker)(NiteUserTrackerHandle *);
typedef NiteStatus (*PFN_niteShutdownUserTracker)(NiteUserTrackerHandle);
typedef NiteStatus (*PFN_niteStartSkeletonTracking)(NiteUserTrackerHandle, NiteUserId);
typedef void (*PFN_niteStopSkeletonTracking)(NiteUserTrackerHandle, NiteUserId);
typedef NiteStatus (*PFN_niteSetSkeletonSmoothing)(NiteUserTrackerHandle, float);
typedef NiteStatus (*PFN_niteReadUserTrackerFrame)(NiteUserTrackerHandle, NiteUserTrackerFrame **);
typedef NiteStatus (*PFN_niteUserTrackerFrameRelease)(NiteUserTrackerHandle, NiteUserTrackerFrame *);

} // extern "C"
