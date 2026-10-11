#!/usr/bin/env bash
# Compila o código nativo no Linux do host (sem NDK) para verificar erros de
# compilação e ligação. Não substitui o build Android: só valida o C/C++.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-host-native}"
cmake -S "$ROOT/app/src/main/cpp" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
      -DK3D_HOST_LIBUSB_CONFIG_DIR="$ROOT/scripts/host_config" >/dev/null
cmake --build "$BUILD" -j"$(nproc)"
(cd "$BUILD" && ctest --output-on-failure)
echo "OK: código nativo compilado e ligado no host em $BUILD"
