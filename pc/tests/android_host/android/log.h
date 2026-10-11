// STUB para o teste no host (android_mode_test): escreve no stderr.
#pragma once
#include <stdio.h>
enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6 };
static inline int __android_log_write(int prio, const char *tag, const char *msg) {
    return fprintf(stderr, "%d %s: %s\n", prio, tag, msg);
}
