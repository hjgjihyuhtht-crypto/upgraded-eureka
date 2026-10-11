#pragma once
// fopen que aceita caminhos UTF-8 também no Windows (nomes de usuário com acento).
#include <cstdio>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace k3d {

inline FILE *openFileUtf8(const std::string &path, const char *mode) {
#ifdef _WIN32
    auto widen = [](const std::string &s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
        return w;
    };
    return _wfopen(widen(path).c_str(), widen(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

} // namespace k3d
