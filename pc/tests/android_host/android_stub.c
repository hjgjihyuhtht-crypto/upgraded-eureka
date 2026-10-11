#include <stdlib.h>
typedef struct ANativeActivity { const char *internalDataPath; const char *externalDataPath; } ANativeActivity;
struct android_app { ANativeActivity *activity; int destroyRequested; };
static ANativeActivity act;
static struct android_app app;
struct android_app *GetAndroidApp(void) {
    act.internalDataPath = getenv("AND_INTERNAL");
    act.externalDataPath = getenv("AND_EXTERNAL");
    app.activity = &act;
    return &app;
}
