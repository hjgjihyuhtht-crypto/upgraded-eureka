#include "app/fileio.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

#include "portable_io.h"

namespace fs = std::filesystem;

namespace k3d {

bool writeFileUtf8(const std::string &path, const void *data, size_t size) {
    FILE *f = openFileUtf8(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data, 1, size, f) == size;
    return std::fclose(f) == 0 && ok;
}

bool readFileUtf8(const std::string &path, std::vector<unsigned char> &out) {
    FILE *f = openFileUtf8(path, "rb");
    if (!f) return false;
    out.clear();
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

bool makeDirs(const std::string &path) {
    std::error_code ec;
    fs::create_directories(fs::u8path(path), ec);
    return fs::is_directory(fs::u8path(path), ec);
}

bool removeFileUtf8(const std::string &path) {
    std::error_code ec;
    return fs::remove(fs::u8path(path), ec);
}

std::string nowStamp() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    return buf;
}

#ifdef _WIN32
static std::string narrow(const wchar_t *w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

std::string absolutePath(const std::string &path) {
    std::error_code ec;
    const fs::path p = fs::absolute(fs::u8path(path), ec);
    return ec ? path : p.u8string();
}

std::string envUtf8(const char *name) {
#ifdef _WIN32
    std::wstring wname(name, name + std::char_traits<char>::length(name));
    wchar_t buf[2048];
    const DWORD n = GetEnvironmentVariableW(wname.c_str(), buf, 2048);
    if (n == 0 || n >= 2048) return "";
    return narrow(buf);
#else
    const char *v = std::getenv(name);
    return v ? v : "";
#endif
}

std::string documentsFolder() {
#ifdef _WIN32
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p)) && p) out = narrow(p);
    if (p) CoTaskMemFree(p);
    if (out.empty()) out = envUtf8("USERPROFILE") + "\\Documents";
    return out;
#else
    return envUtf8("HOME");
#endif
}

std::vector<std::string> listFiles(const std::string &dir, const std::string &ext) {
    std::vector<std::pair<fs::file_time_type, std::string>> v;
    std::error_code ec;
    for (auto it = fs::directory_iterator(fs::u8path(dir), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string e = it->path().extension().u8string();
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (e != ext) continue;
        v.push_back({it->last_write_time(ec), it->path().u8string()});
    }
    std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (auto &p : v) out.push_back(p.second);
    return out;
}

void openFolder(const std::string &path) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__ANDROID__)
    (void)path;  // no Android as fotos aparecem na galeria do próprio jogo
#else
    const std::string cmd = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
    if (std::system(cmd.c_str()) != 0) {}
#endif
}

} // namespace k3d
