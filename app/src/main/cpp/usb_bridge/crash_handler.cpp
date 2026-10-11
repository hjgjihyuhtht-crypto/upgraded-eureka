// Diagnóstico: se o código nativo cair (SIGSEGV, SIGABRT...), grava o sinal e a pilha num arquivo
// que o app mostra na próxima abertura (CrashReport.kt). Depois devolve o sinal ao Android.
#include <dlfcn.h>
#include <fcntl.h>
#include <jni.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <unwind.h>

#include <cstdio>
#include <string>

#include "jni_common.h"

namespace {
char gPath[512];
struct sigaction gOld[32];
const int kSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};

struct Frames { uintptr_t pc[48]; int n; };

_Unwind_Reason_Code unwindCb(struct _Unwind_Context *ctx, void *arg) {
    auto *f = static_cast<Frames *>(arg);
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && f->n < 48) f->pc[f->n++] = pc;
    return f->n < 48 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void writeStr(int fd, const char *s) { if (s) (void)!write(fd, s, strlen(s)); }

void handler(int sig, siginfo_t *info, void *uctx) {
    const int fd = open(gPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char line[512];
        snprintf(line, sizeof(line), "sinal %d (%s), endereço %p\npilha:\n", sig, strsignal(sig), info ? info->si_addr : nullptr);
        writeStr(fd, line);
        Frames fr{};
        _Unwind_Backtrace(unwindCb, &fr);
        for (int i = 0; i < fr.n; ++i) {
            Dl_info di{};
            if (dladdr(reinterpret_cast<void *>(fr.pc[i]), &di) && di.dli_fname) {
                const char *lib = strrchr(di.dli_fname, '/');
                snprintf(line, sizeof(line), "#%02d %s +0x%lx %s+0x%lx\n", i, lib ? lib + 1 : di.dli_fname,
                         static_cast<unsigned long>(fr.pc[i] - reinterpret_cast<uintptr_t>(di.dli_fbase)),
                         di.dli_sname ? di.dli_sname : "?",
                         static_cast<unsigned long>(di.dli_saddr ? fr.pc[i] - reinterpret_cast<uintptr_t>(di.dli_saddr) : 0));
            } else {
                snprintf(line, sizeof(line), "#%02d 0x%lx\n", i, static_cast<unsigned long>(fr.pc[i]));
            }
            writeStr(fd, line);
        }
        close(fd);
    }
    // devolve ao tratador anterior (debuggerd do Android)
    sigaction(sig, &gOld[sig], nullptr);
    raise(sig);
}
} // namespace

extern "C" JNIEXPORT void JNICALL JNI_FN(nativeInstallCrashHandler)(JNIEnv *env, jobject, jstring path) {
    const char *c = env->GetStringUTFChars(path, nullptr);
    snprintf(gPath, sizeof(gPath), "%s", c ? c : "");
    if (c) env->ReleaseStringUTFChars(path, c);
    static bool installed = false;
    if (installed) return;
    installed = true;
    static char altStack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    sigaltstack(&ss, nullptr);
    for (int s : kSignals) {
        struct sigaction sa{};
        sa.sa_sigaction = handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(s, &sa, &gOld[s]);
    }
}
