#pragma once
// Arquivos e pastas com caminhos UTF-8 (inclusive no Windows). Sem raylib aqui.
#include <string>
#include <vector>

namespace k3d {

bool writeFileUtf8(const std::string &path, const void *data, size_t size);
bool readFileUtf8(const std::string &path, std::vector<unsigned char> &out);
bool makeDirs(const std::string &path);
bool removeFileUtf8(const std::string &path);
std::string nowStamp();                          // aaaaMMdd_HHmmss
std::string envUtf8(const char *name);
std::string documentsFolder();                   // pasta Documentos do usuário
// Arquivos de uma pasta com a extensão dada (sem diferenciar maiúsculas), mais novos primeiro.
std::vector<std::string> listFiles(const std::string &dir, const std::string &ext);
// Caminho absoluto (relativo à pasta atual no momento da chamada).
std::string absolutePath(const std::string &path);
// Abre a pasta no gerenciador de arquivos do sistema.
void openFolder(const std::string &path);

} // namespace k3d
