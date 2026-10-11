// STUB para o teste no host (android_mode_test). O build Android de verdade usa o do NDK.
#pragma once
typedef struct ANativeActivity { const char *internalDataPath; const char *externalDataPath; } ANativeActivity;
struct android_app { ANativeActivity *activity; int destroyRequested; };
